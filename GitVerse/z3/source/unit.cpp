#include <gitproc/unit.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace fs = std::filesystem;

namespace gitproc {

static std::string trim(std::string s){
  while(!s.empty() && (s.back()==' '||s.back()=='\t'||s.back()=='\r'||s.back()=='\n')) s.pop_back();
  size_t i=0; while(i<s.size() && (s[i]==' '||s[i]=='\t')) ++i; return s.substr(i);
}

std::string Unit::name() const {
  auto f = path.filename().string();
  auto pos = f.find_last_of('.');
  return pos==std::string::npos ? f : f.substr(0,pos);
}

static std::vector<std::string> split_cmd(const std::string& s){
  std::vector<std::string> out;
  std::string cur; bool in_single=false, in_double=false, esc=false;
  for(char c: s){
    if (esc){ cur.push_back(c); esc=false; continue; }
    if (c=='\\'){ esc=true; continue; }
    if (c=='\'' && !in_double){ in_single=!in_single; continue; }
    if (c=='"'  && !in_single){ in_double=!in_double; continue; }
    if (!in_single && !in_double && (c==' '||c=='\t')){
      if (!cur.empty()){ out.push_back(cur); cur.clear(); }
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

static void load_env_file(Unit& u, const fs::path& file){
  std::ifstream in(file);
  if (!in) { spdlog::warn("[unit={}] EnvironmentFile not found: {}", u.name(), file.string()); return; }
  std::string line;
  while (std::getline(in, line)) {
    auto s = trim(line);
    if (s.empty() || s[0]=='#' || s[0]==';') continue;
    auto pos = s.find('=');
    if (pos==std::string::npos) continue;
    auto k = trim(s.substr(0,pos));
    auto v = trim(s.substr(pos+1));
    if (!k.empty()) u.env[k] = v;
  }
}

static RestartPolicy parse_restart(std::string v){
  for (auto& ch: v) ch = (char)std::tolower((unsigned char)ch);
  if (v=="always") return RestartPolicy::Always;
  if (v=="on-failure" || v=="onfailure") return RestartPolicy::OnFailure;
  if (v=="never" || v=="no" || v=="false" || v=="0") return RestartPolicy::Never;
  spdlog::warn("Unknown Restart policy '{}', falling back to 'never'", v);
  return RestartPolicy::Never;
}

static void append_shell_list(std::vector<std::string>& dst, const std::string& val){
  std::stringstream ss(val); std::string one;
  while (std::getline(ss, one, ';')) {
    one = trim(one);
    if (!one.empty()) dst.push_back(one);
  }
}

static void append_name_list(std::vector<std::string>& dst, const std::string& val){
  std::stringstream ss(val); std::string one;
  while (std::getline(ss, one, ';')) {
    one = trim(one);
    if (!one.empty()) dst.push_back(one);
  }
}

Unit Unit::Load(const fs::path& p) {
  Unit u; u.path = fs::absolute(p);
  std::ifstream in(u.path);
  if (!in) throw std::runtime_error("Unit file not found: " + p.string());

  bool in_service=false;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0]=='#' || line[0]==';') continue;
    if (line.front()=='[' && line.back()==']') {
      in_service = (line == "[Service]");
      continue;
    }
    if (!in_service) continue;

    auto eq = line.find('=');
    if (eq==std::string::npos) continue;
    auto key = trim(line.substr(0,eq));
    auto val = trim(line.substr(eq+1));

    if (key=="ExecStart") {
      u.exec = split_cmd(val);

    } else if (key=="ExecStartPre") {
      append_shell_list(u.exec_start_pre, val);

    } else if (key=="ExecStartPost") {
      append_shell_list(u.exec_start_post, val);

    } else if (key=="WorkingDirectory") {
      if (!val.empty()) u.working_dir = fs::path(val);

    } else if (key=="Environment") {
      std::stringstream ss(val); std::string kv;
      while (std::getline(ss, kv, ';')) {
        auto pos = kv.find('=');
        if (pos!=std::string::npos) {
          auto k = trim(kv.substr(0,pos));
          auto v = trim(kv.substr(pos+1));
          if (!k.empty()) u.env[k] = v;
        }
      }

    } else if (key=="EnvironmentFile") {
      std::stringstream ss(val); std::string one;
      while (std::getline(ss, one, ';')) {
        one = trim(one);
        if (one.empty()) continue;
        fs::path ef = one;
        if (!ef.is_absolute()) ef = u.path.parent_path() / ef;
        u.env_files.push_back(fs::absolute(ef));
      }

    } else if (key=="PIDFile") {
      fs::path pf = val;
      if (!pf.is_absolute()) {
        if (!u.working_dir.empty()) pf = u.working_dir / pf;
        else pf = u.path.parent_path() / pf;
      }
      u.pid_file = fs::absolute(pf);

    } else if (key=="PIDFileTimeoutSec") {
      try { u.pidfile_timeout_sec = std::stoi(val); } catch(...) {}

    } else if (key=="TimeoutStopSec") {
      try { u.timeout_stop_sec = std::stoi(val); } catch(...) {}

    } else if (key=="ExecReload") {
      u.exec_reload = val;

    } else if (key=="ExecStop") {
      u.exec_stop = val;

    } else if (key=="ExecHealth") {
      u.exec_health = split_cmd(val);

    } else if (key=="WatchdogSec") {
      try { u.watchdog_sec = std::stoi(val); } catch(...) {}

    } else if (key=="HealthHttpUrl") {
      u.health_http_url = val;

    } else if (key=="HealthHttpTimeoutMs") {
      try { u.health_http_timeout_ms = std::stoi(val); } catch(...) {}

    } else if (key=="HealthHttpExpect") {
      u.health_http_expect = val;

    } else if (key=="Restart") {
      u.restart = parse_restart(val);

    } else if (key=="RestartSec") {
      try { u.restart_sec = std::stoi(val); } catch(...) {}

    } else if (key=="RestartWindowSec") {
      try { u.restart_window_sec = std::stoi(val); } catch(...) {}

    } else if (key=="MaxRestartsInWindow") {
      try { u.max_restarts_in_window = std::stoi(val); } catch(...) {}

    } else if (key=="Before") {
      append_name_list(u.before, val);

    } else if (key=="After") {
      append_name_list(u.after, val);
    }
  }

  if (u.exec.empty()) throw std::runtime_error("ExecStart is required");
  if (!u.working_dir.empty() && !fs::exists(u.working_dir))
    throw std::runtime_error("WorkingDirectory not found: " + u.working_dir.string());

  for (auto& ef : u.env_files) load_env_file(u, ef);

  return u;
}

} // namespace gitproc
