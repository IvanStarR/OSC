#include <gitproc/process.hpp>
#include <gitproc/unit.hpp>
#include <gitproc/state.hpp>
#include <gitproc/io.hpp>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

namespace gitproc {

static bool pid_alive(int pid){
  if (pid <= 0) return false;
  return ::kill(pid, 0) == 0 || errno == EPERM;
}

static int make_cloexec_pipe(int pfd[2]) {
#ifdef __linux__
  if (::pipe2(pfd, O_CLOEXEC) == 0) return 0;
#endif
  if (::pipe(pfd) != 0) return -1;
  ::fcntl(pfd[0], F_SETFD, ::fcntl(pfd[0], F_GETFD) | FD_CLOEXEC);
  ::fcntl(pfd[1], F_SETFD, ::fcntl(pfd[1], F_GETFD) | FD_CLOEXEC);
  return 0;
}

// простая обёртка: выполнить shell-команду в нужной CWD и с env
static int exec_shell_env(const std::string& cmd,
                          const fs::path& cwd,
                          const std::unordered_map<std::string,std::string>& env){
  pid_t pid = ::fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (!cwd.empty()) ::chdir(cwd.c_str());
    for (auto& [k,v] : env) ::setenv(k.c_str(), v.c_str(), 1);
    ::execl("/bin/sh","sh","-c",cmd.c_str(), (char*)nullptr);
    _exit(127);
  }
  int st=0; ::waitpid(pid,&st,0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

int ProcessRunner::start(const Unit& u,
                         const fs::path& run_dir,
                         const fs::path& logs_dir) {
  const auto name = u.name();
  const auto pidf = StateStore::pid_file(name, run_dir);

  io::ensure_dir(logs_dir);
  io::ensure_dir(run_dir);

  if (auto old = StateStore::read_pid(pidf)) {
    if (pid_alive(*old)) {
      spdlog::warn("[unit={}] already running pid={}", name, *old);
      return 0;
    }
  }

  // pre-hooks
  for (const auto& cmd : u.exec_start_pre) {
    spdlog::info("[unit={}] ExecStartPre: {}", name, cmd);
    int rc = exec_shell_env(cmd, u.working_dir.empty()?u.path.parent_path():u.working_dir, u.env);
    if (rc != 0) {
      spdlog::error("[unit={}] ExecStartPre failed rc={}", name, rc);
      return 1;
    }
  }

  const auto outp = logs_dir / (name + ".out");
  const auto errp = logs_dir / (name + ".err");

  std::uintmax_t max_mb = 5;
  if (const char* env = ::getenv("GITPROC_LOG_MAX_MB")) {
    long v = std::strtol(env, nullptr, 10);
    if (v >= 0) max_mb = static_cast<std::uintmax_t>(v);
  }
  io::rotate_logs(outp, max_mb * 1024 * 1024, 3);
  io::rotate_logs(errp, max_mb * 1024 * 1024, 3);

  int outfd = -1, errfd = -1;
  try {
    outfd = io::open_for_stdout(outp);
    errfd = io::open_for_stderr(errp);
  } catch (const std::exception& e) {
    spdlog::error("[unit={}] log open failed: {}", name, e.what());
    if (outfd>=0) ::close(outfd);
    if (errfd>=0) ::close(errfd);
    return 1;
  }

  int pfd[2];
  if (make_cloexec_pipe(pfd) != 0) {
    spdlog::error("[unit={}] pipe failed: {}", name, strerror(errno));
    ::close(outfd); ::close(errfd);
    return 1;
  }

  // --- NEW: снимем mtime PID-файла до старта, чтобы не принять устаревшее значение
  std::optional<fs::file_time_type> pidfile_mtime_before;
  if (!u.pid_file.empty()) {
    std::error_code ec;
    if (fs::exists(u.pid_file, ec)) {
      pidfile_mtime_before = fs::last_write_time(u.pid_file, ec);
    }
  }

  pid_t pid = ::fork();
  if (pid < 0) {
    spdlog::error("[unit={}] fork failed: {}", name, strerror(errno));
    ::close(outfd); ::close(errfd);
    ::close(pfd[0]); ::close(pfd[1]);
    return 1;
  }

  if (pid == 0) {
    ::close(pfd[0]);

    if (!u.working_dir.empty()) ::chdir(u.working_dir.c_str());
    for (auto& [k,v] : u.env) ::setenv(k.c_str(), v.c_str(), 1);

    ::dup2(outfd, STDOUT_FILENO);
    ::dup2(errfd, STDERR_FILENO);
    ::close(outfd); ::close(errfd);

    std::vector<char*> argv;
    argv.reserve(u.exec.size()+1);
    for (auto& s : u.exec) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);

    ::execvp(argv[0], argv.data());

    int err = errno;
    (void)!::write(pfd[1], &err, sizeof(err));
    ::close(pfd[1]);
    _exit(127);
  }

  ::close(outfd); ::close(errfd);
  ::close(pfd[1]);

  int child_errno = 0;
  ssize_t n = ::read(pfd[0], &child_errno, sizeof(child_errno));
  ::close(pfd[0]);

  if (n > 0) {
    int status = 0;
    ::waitpid(pid, &status, 0);
    spdlog::error("[unit={}] exec failed (errno={}): {}", name, child_errno, strerror(child_errno));
    return 1;
  }

  // Если задан PIDFile — дождёмся обновления файла и подменим PID
  pid_t final_pid = pid;
  if (!u.pid_file.empty()) {
    spdlog::info("[unit={}] waiting PIDFile: {}", name, u.pid_file.string());
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(u.pidfile_timeout_sec);

    while (std::chrono::steady_clock::now() < deadline) {
      std::error_code ec;
      if (fs::exists(u.pid_file, ec)) {
        // проверим, что файл обновился после старта
        auto mtime_now = fs::last_write_time(u.pid_file, ec);
        bool is_updated = true;
        if (pidfile_mtime_before.has_value() && !ec) {
          is_updated = (mtime_now > *pidfile_mtime_before);
        }
        std::ifstream pf(u.pid_file);
        if (pf) {
          long p = -1;
          pf >> p;
          if (p > 0 && is_updated) {  // принимаем только «свежее» значение
            final_pid = static_cast<pid_t>(p);
            break;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (final_pid != pid) {
      spdlog::info("[unit={}] PIDFile detected pid={}", name, final_pid);
      // дождёмся, что child завершился (в случае демона-родителя)
      int st=0; ::waitpid(pid, &st, WNOHANG);
    } else {
      spdlog::warn("[unit={}] PIDFile not found/updated in {}s; continue with fork pid={}",
                   name, u.pidfile_timeout_sec, pid);
    }
  }

  (void)StateStore::write_pid(pidf, final_pid);
  spdlog::info("[unit={}] started pid={}", name, final_pid);

  // post-hooks (выполняем уже после успешного старта)
  for (const auto& cmd : u.exec_start_post) {
    spdlog::info("[unit={}] ExecStartPost: {}", name, cmd);
    int rc = exec_shell_env(cmd, u.working_dir.empty()?u.path.parent_path():u.working_dir, u.env);
    if (rc != 0) {
      spdlog::warn("[unit={}] ExecStartPost failed rc={}", name, rc);
      // не падаем: сервис уже запущен
    }
  }

  return 0;
}

bool ProcessRunner::stop(const Unit& u, const fs::path& run_dir) {
  const auto name = u.name();
  const auto pidf = StateStore::pid_file(name, run_dir);
  auto pid = StateStore::read_pid(pidf);
  if (!pid || !pid_alive(*pid)) {
    spdlog::info("[unit={}] not running", name);
    return true;
  }

  // Сначала ExecStop, если задан
  if (!u.exec_stop.empty()) {
    std::string cmd = u.exec_stop;
    if (auto pos = cmd.find("%p"); pos != std::string::npos) {
      cmd.replace(pos, 2, std::to_string(*pid));
    }
    spdlog::info("[unit={}] ExecStop...", name);
    int rc = exec_shell_env(cmd, u.working_dir.empty()?u.path.parent_path():u.working_dir, u.env);
    if (rc == 0) {
      int waited = 0;
      while (waited < u.timeout_stop_sec) {
        int status=0; pid_t r = ::waitpid(*pid, &status, WNOHANG);
        if (r == *pid) {
          int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
          StateStore::write_status_json(StateStore::status_file(name, run_dir), *pid, exit_code);
          spdlog::info("[unit={}] stopped via ExecStop exit={}", name, exit_code);
          ::unlink(pidf.c_str());
          return true;
        }
        ::sleep(1); waited++;
      }
      // не успел — перейдём к TERM/KILL
    } else {
      spdlog::warn("[unit={}] ExecStop failed (rc={})", name, rc);
    }
  }

  spdlog::info("[unit={}] stopping pid={} (SIGTERM, timeout={}s)", name, *pid, u.timeout_stop_sec);
  ::kill(*pid, SIGTERM);

  int waited = 0;
  while (waited < u.timeout_stop_sec) {
    int status=0;
    pid_t r = ::waitpid(*pid, &status, WNOHANG);
    if (r == *pid) {
      int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      StateStore::write_status_json(StateStore::status_file(name, run_dir), *pid, exit_code);
      spdlog::info("[unit={}] stopped exit={}", name, exit_code);
      ::unlink(pidf.c_str());
      return true;
    }
    ::sleep(1); waited++;
  }

  spdlog::warn("[unit={}] force kill pid={}", name, *pid);
  ::kill(*pid, SIGKILL);
  int status=0; ::waitpid(*pid, &status, 0);
  StateStore::write_status_json(StateStore::status_file(name, run_dir), *pid, 137);
  ::unlink(pidf.c_str());
  return true;
}

ProcStatus ProcessRunner::status(const Unit& u, const fs::path& run_dir) {
  ProcStatus s{};
  const auto name = u.name();
  const auto pidf = StateStore::pid_file(name, run_dir);
  auto pid = StateStore::read_pid(pidf);

  // если есть PIDFile — можно «освежить» PID (на случай внешних рестартов)
  if (!u.pid_file.empty()) {
    std::ifstream pf(u.pid_file);
    long p=-1; if (pf) { pf>>p; if (p>0) pid = static_cast<int>(p); }
  }

  if (pid && pid_alive(*pid)) {
    s.running = true; s.pid = *pid; s.last_exit_code = 0;
  } else {
    s.running = false; s.pid = pid.value_or(-1); s.last_exit_code = 0;
  }
  return s;
}

bool ProcessRunner::reload(const Unit& u,
                           const fs::path& run_dir,
                           const fs::path& logs_dir) {
  (void)logs_dir;
  const auto name = u.name();
  const auto pidf = StateStore::pid_file(name, run_dir);
  auto pid = StateStore::read_pid(pidf);
  if (!pid || !pid_alive(*pid)) {
    spdlog::info("[unit={}] not running; fallback to start", name);
    return start(u, run_dir, logs_dir) == 0;
  }

  if (u.exec_reload.empty()) {
    spdlog::info("[unit={}] ExecReload not set; fallback to restart", name);
    (void)stop(u, run_dir);
    return start(u, run_dir, logs_dir) == 0;
  }

  std::string cmd = u.exec_reload;
  if (auto pos = cmd.find("%p"); pos != std::string::npos) {
    cmd.replace(pos, 2, std::to_string(*pid));
  }
  int rc = exec_shell_env(cmd, u.working_dir.empty()?u.path.parent_path():u.working_dir, u.env);
  if (rc == 0) {
    spdlog::info("[unit={}] reloaded via ExecReload", name);
    return true;
  } else {
    spdlog::warn("[unit={}] ExecReload failed (rc={}); fallback to restart", name, rc);
    (void)stop(u, run_dir);
    return start(u, run_dir, logs_dir) == 0;
  }
}

} // namespace gitproc
