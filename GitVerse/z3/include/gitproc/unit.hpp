#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

namespace gitproc {

enum class RestartPolicy { Never, OnFailure, Always };

struct Unit {
  std::filesystem::path path;

  // parsed fields
  std::vector<std::string> exec; // ExecStart split
  std::vector<std::string> exec_start_pre;
  std::vector<std::string> exec_start_post;
  std::string exec_reload;
  std::string exec_stop;
  std::vector<std::string> exec_health;

  std::filesystem::path working_dir;
  std::unordered_map<std::string,std::string> env;
  std::vector<std::filesystem::path> env_files;

  std::filesystem::path pid_file;
  int pidfile_timeout_sec = 2;
  int timeout_stop_sec = 5;

  int watchdog_sec = 0;
  std::string health_http_url;
  int health_http_timeout_ms = 800;
  std::string health_http_expect;

  RestartPolicy restart = RestartPolicy::Never;
  int restart_sec = 1;
  int restart_window_sec = 10;
  int max_restarts_in_window = 5;

  // NEW: зависимости
  std::vector<std::string> before;
  std::vector<std::string> after;

  std::string name() const;
  static Unit Load(const std::filesystem::path& p);
};

} // namespace gitproc
