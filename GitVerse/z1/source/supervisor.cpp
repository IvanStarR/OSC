#include <gitproc/supervisor.hpp>
#include <spdlog/spdlog.h>

#include <thread>
#include <chrono>
#include <atomic>
#include <deque>
#include <vector>
#include <string>
#include <set>
#include <system_error>
#include <fstream>
#include <optional>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace gitproc {

// ------------------------ Конструирование ------------------------

Supervisor::Supervisor(fs::path run_dir, fs::path logs_dir)
  : run_dir_(std::move(run_dir)),
    logs_dir_(std::move(logs_dir)),
    repo_workdir_(fs::current_path()),
    repo_(GitRepo::open_local(fs::current_path())) {}

Supervisor::Supervisor(fs::path repo_workdir)
  : run_dir_("run"),
    logs_dir_("logs"),
    repo_workdir_(std::move(repo_workdir)),
    repo_(GitRepo::open_local(repo_workdir_)) { }

Supervisor::~Supervisor() { stop(); }

// ------------------------ Старый API ------------------------

bool Supervisor::start(const Unit& u){
  stop();
  if (ProcessRunner::start(u, run_dir_, logs_dir_) != 0) return false;

  ProcState st{};
  auto s = ProcessRunner::status(u, run_dir_);
  st.running = s.running; st.pid = s.pid; st.last_exit_code = s.last_exit_code;
  proc_state_[u.name()] = st;
  last_unit_path_[u.name()] = u.path;

  stop_flag_ = false;
  exit_thread_ = std::thread([this,u](){ monitor_exit_loop(u); });
  if ((!u.exec_health.empty() || !u.health_http_url.empty()) && u.watchdog_sec > 0) {
    health_thread_ = std::thread([this,u](){ health_loop(u); });
  }
  return true;
}

bool Supervisor::reload(const Unit& u){
  bool ok = ProcessRunner::reload(u, run_dir_, logs_dir_);
  auto s = ProcessRunner::status(u, run_dir_);
  ProcState st{ s.pid, s.running, s.last_exit_code };
  proc_state_[u.name()] = st;
  last_unit_path_[u.name()] = u.path;
  return ok;
}

bool Supervisor::stop(){
  stop_flag_ = true;
  if (health_thread_.joinable()) health_thread_.join();
  if (exit_thread_.joinable())   exit_thread_.join();
  return true;
}

// ------------------------ Git API ------------------------

bool Supervisor::open_repo(const std::string& url_or_path, const std::string& branch) {
  repo_branch_ = branch.empty() ? "main" : branch;
  fs::create_directories(repo_workdir_);
  try {
    repo_ = GitRepo::open(url_or_path);
    repo_opened_ = true;
    return true;
  } catch (const std::exception& e) {
    spdlog::error("open_repo failed: {}", e.what());
    return false;
  }
}

// reconcile: добавленные → start, удалённые → stop, изменённые → reload (при head_changed)
bool Supervisor::sync_and_apply() {
  if (!repo_opened_) return false;
  bool head_changed = repo_.pull(repo_branch_);

  std::set<std::string> have;
  for (auto& name : scan_service_names()) have.insert(name);

  std::set<std::string> was;
  for (auto& kv : last_unit_path_) was.insert(kv.first);

  // удалённые
  for (auto& n : was) if (!have.count(n)) {
    stop(n);
    last_unit_path_.erase(n);
    proc_state_.erase(n);
  }

  // добавленные: учтём зависимости — строим граф на наборе have
  std::vector<std::string> names(have.begin(), have.end());
  auto units = load_units_by_names(names);
  DepGraph g = build_dep_graph(units);
  std::vector<std::string> order;
  try { order = topo_sort(g); }
  catch(...) { order = names; } // на случай циклов — best effort

  for (auto& n : order) if (!was.count(n)) {
    start(n);
  }

  // изменённые: если HEAD поменялся — перезагрузим существующие по обратному топопорядку (чтобы нижние перезапустились позже)
  if (head_changed) {
    for (auto it = order.begin(); it != order.end(); ++it) {
      const auto& n = *it;
      if (was.count(n)) reload(n);
    }
  }

  return true;
}

bool Supervisor::start_all() {
  auto names = scan_service_names();
  auto units = load_units_by_names(names);
  DepGraph g = build_dep_graph(units);
  std::vector<std::string> order;
  try { order = topo_sort(g); } catch(...) { order = names; }

  bool ok = true;
  for (auto& n : order) ok &= start(n);
  return ok;
}

bool Supervisor::rollback_unit(const std::string& name, const std::string& commit) {
  if (!repo_opened_) return false;
  fs::path rel = fs::path("services") / (name + ".service");
  if (!fs::exists(repo_.root() / rel)) {
    rel = fs::path("services") / (name + ".unit");
  }
  fs::path abs = repo_.root() / rel;
  fs::create_directories(abs.parent_path());

  std::string cmd = "git -C \"" + repo_.root().string() + "\" show " + commit + ":" + rel.string() +
                    " > \"" + abs.string() + "\"";
  int rc = std::system(cmd.c_str());
  if (rc != 0) return false;

  return reload(name);
}

// ------------------------ Императивные по имени ------------------------

bool Supervisor::start(const std::string& name) {
  fs::path up = resolve_unit_path_by_name(name);
  Unit u = Unit::Load(up);
  bool ok = spawn_unit(u);
  if (ok) {
    last_unit_path_[name] = up;
    auto s = ProcessRunner::status(u, run_dir_);
    proc_state_[name] = { s.pid, s.running, s.last_exit_code };
  }
  return ok;
}

bool Supervisor::stop(const std::string& name) {
  auto it = last_unit_path_.find(name);
  if (it == last_unit_path_.end()) return true;
  Unit u = Unit::Load(it->second);

  ProcState st{};
  bool ok = stop_unit(u, &st);
  proc_state_[name] = st;
  return ok;
}

bool Supervisor::reload(const std::string& name) {
  auto it = last_unit_path_.find(name);
  fs::path up;
  if (it == last_unit_path_.end()) {
    up = resolve_unit_path_by_name(name);
    last_unit_path_[name] = up;
  } else up = it->second;

  Unit u = Unit::Load(up);
  bool ok = reload(u);
  auto s = ProcessRunner::status(u, run_dir_);
  proc_state_[name] = { s.pid, s.running, s.last_exit_code };
  return ok;
}

std::unordered_map<std::string, ProcState> Supervisor::status() const {
  return proc_state_;
}

// ------------------------ Вспомогательные ------------------------

fs::path Supervisor::resolve_unit_path_by_name(const std::string& name) const {
  if (repo_opened_) {
    fs::path p = repo_.resolve_unit(name);
    if (p.is_absolute()) return p;
    return repo_.root() / p;
  }
  fs::path cand1 = fs::path("services") / (name + ".service");
  fs::path cand2 = fs::path("services") / (name + ".unit");
  if (fs::exists(cand1)) return fs::absolute(cand1);
  if (fs::exists(cand2)) return fs::absolute(cand2);
  return fs::absolute(name);
}

std::vector<std::string> Supervisor::scan_service_names() const {
  std::vector<std::string> out;
  fs::path base = repo_opened_ ? (repo_.root() / "services") : fs::path("services");
  std::error_code ec;
  if (!fs::exists(base, ec)) return out;

  for (auto& e : fs::directory_iterator(base, ec)) {
    if (!e.is_regular_file()) continue;
    auto ext = e.path().extension().string();
    if (ext == ".service" || ext == ".unit") {
      out.push_back(e.path().stem().string());
    }
  }
  return out;
}

bool Supervisor::spawn_unit(const Unit& u) {
  int rc = ProcessRunner::start(u, run_dir_, logs_dir_);
  return rc == 0;
}

bool Supervisor::stop_unit(const Unit& u, ProcState* out_state) {
  bool ok = ProcessRunner::stop(u, run_dir_);
  auto s = ProcessRunner::status(u, run_dir_);
  if (out_state) *out_state = { s.pid, s.running, s.last_exit_code };
  return ok;
}

// --- зависимости ---

std::vector<Unit> Supervisor::load_units_by_names(const std::vector<std::string>& names) const {
  std::vector<Unit> v; v.reserve(names.size());
  for (const auto& n : names) {
    fs::path up = resolve_unit_path_by_name(n);
    if (fs::exists(up)) {
      try { v.push_back(Unit::Load(up)); } catch(...) {}
    }
  }
  return v;
}

DepGraph Supervisor::build_dep_graph(const std::vector<Unit>& units) const {
  DepGraph g;
  // заполним вершины
  for (const auto& u : units) { g.try_emplace(u.name(), std::vector<std::string>{}); }
  // ребра:
  // After=X => X -> this
  // Before=Y => this -> Y
  for (const auto& u : units) {
    auto& edges = g[u.name()];
    for (const auto& x : u.after) {
      if (g.find(x) != g.end()) {
        // X -> this : реализуем, увеличив вход this; для Kahn — просто добавим ребро X->this
        // хранить будем в списке исходящих у X
        g[x].push_back(u.name());
      }
    }
    for (const auto& y : u.before) {
      if (g.find(y) != g.end()) {
        edges.push_back(y); // this -> Y
      }
    }
  }
  return g;
}

// ------------------------ Мониторинг/здоровье ------------------------

void Supervisor::monitor_exit_loop(Unit u){
  using clock = std::chrono::steady_clock;
  std::deque<clock::time_point> restarts;

  while (!stop_flag_) {
    auto st = ProcessRunner::status(u, run_dir_);
    proc_state_[u.name()] = { st.pid, st.running, st.last_exit_code };

    if (!st.running) {
      bool should_restart = false;
      if (u.restart == RestartPolicy::Always) should_restart = true;
      else if (u.restart == RestartPolicy::OnFailure) {
        should_restart = (st.last_exit_code != 0);
      }
      if (should_restart) {
        auto now = clock::now();
        while (!restarts.empty() &&
               (now - restarts.front()) > std::chrono::seconds(u.restart_window_sec)) {
          restarts.pop_front();
        }
        restarts.push_back(now);
        if ((int)restarts.size() > u.max_restarts_in_window) {
          spdlog::warn("[unit={}] too many restarts; backing off {}s", u.name(), u.restart_window_sec);
          std::this_thread::sleep_for(std::chrono::seconds(u.restart_window_sec));
        } else {
          std::this_thread::sleep_for(std::chrono::seconds(u.restart_sec));
        }
        (void)ProcessRunner::start(u, run_dir_, logs_dir_);
      }
    }
    std::this_thread::sleep_for(200ms);
  }
}

static bool http_expect_ok(int code, const std::string& expect){
  if (expect.empty()) return (code>=200 && code<300);

  for(char c: expect){
    if(!(std::isdigit(static_cast<unsigned char>(c)) || c=='-' || c==',' || c==' '))
      return false;
  }

  auto trim = [](std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
  };

  if (expect.find(',') != std::string::npos) {
    std::stringstream ss(expect); std::string tok;
    while (std::getline(ss, tok, ',')) {
      tok = trim(tok);
      if (tok.find('-')!=std::string::npos) {
        int a=0,b=0; std::sscanf(tok.c_str(), "%d-%d", &a, &b);
        if (code>=a && code<=b) return true;
      } else {
        int v=0; std::sscanf(tok.c_str(), "%d", &v);
        if (code==v) return true;
      }
    }
    return false;
  }
  if (expect.find('-') != std::string::npos) {
    int a=0,b=0; std::sscanf(expect.c_str(), "%d-%d", &a, &b);
    return code>=a && code<=b;
  }
  int v=0; std::sscanf(expect.c_str(), "%d", &v);
  return code==v;
}

static std::optional<int> http_get_status(const std::string& url, int timeout_ms){
  if (url.rfind("http://",0)!=0) return std::nullopt;
  std::string rest = url.substr(7);
  std::string host_port, path="/";
  auto slash = rest.find('/');
  if (slash!=std::string::npos){ host_port = rest.substr(0,slash); path = rest.substr(slash); }
  else host_port = rest;

  std::string host = host_port;
  std::string port = "80";
  auto colon = host_port.find(':');
  if (colon!=std::string::npos){ host = host_port.substr(0,colon); port = host_port.substr(colon+1); }

  addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
  addrinfo* res=nullptr;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res)!=0) return std::nullopt;

  int sock = -1; addrinfo* rp=nullptr;
  for (rp=res; rp; rp=rp->ai_next){
    sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock<0) continue;
    timeval tv{}; tv.tv_sec = timeout_ms/1000; tv.tv_usec = (timeout_ms%1000)*1000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(sock, rp->ai_addr, rp->ai_addrlen)==0) break;
    ::close(sock); sock=-1;
  }
  freeaddrinfo(res);
  if (sock<0) return std::nullopt;

  std::string req = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\n\r\n";
  if (::send(sock, req.data(), req.size(), 0) < 0) { ::close(sock); return std::nullopt; }

  char buf[1024]; int n = ::recv(sock, buf, sizeof(buf)-1, 0);
  ::close(sock);
  if (n<=0) return std::nullopt;
  buf[n]=0;
  int code=0; std::sscanf(buf, "HTTP/%*s %d", &code);
  if (code<=0) return std::nullopt;
  return code;
}

void Supervisor::health_loop(Unit u){
  while (!stop_flag_) {
    std::this_thread::sleep_for(std::chrono::seconds(u.watchdog_sec));
    if (stop_flag_) break;

    bool ok_script = true, have_script = !u.exec_health.empty();
    if (have_script) {
      pid_t pid = ::fork();
      if (pid==0){
        if (!u.working_dir.empty()) ::chdir(u.working_dir.c_str());
        std::vector<char*> argv; argv.reserve(u.exec_health.size()+1);
        for (auto& s : u.exec_health) argv.push_back(const_cast<char*>(s.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
      }
      int st=0; ::waitpid(pid, &st, 0);
      ok_script = (WIFEXITED(st) && WEXITSTATUS(st)==0);
    }

    bool ok_http = true, have_http = !u.health_http_url.empty();
    if (have_http) {
      auto stcode = http_get_status(u.health_http_url, u.health_http_timeout_ms);
      ok_http = (stcode.has_value() && http_expect_ok(*stcode, u.health_http_expect));
    }

    bool have_any = have_script || have_http;
    bool healthy = (!have_any) || ( (!have_script || ok_script) && (!have_http || ok_http) );

    if (!healthy) {
      spdlog::warn("[unit={}] health-check failed; restarting", u.name());
      (void)ProcessRunner::stop(u, run_dir_);
      (void)ProcessRunner::start(u, run_dir_, logs_dir_);
    }
  }
}

} // namespace gitproc
