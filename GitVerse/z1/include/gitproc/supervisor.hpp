#pragma once
#include <gitproc/unit.hpp>
#include <gitproc/git.hpp>
#include <gitproc/process.hpp>
#include <gitproc/dependency.hpp>

#include <filesystem>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>

namespace gitproc {

struct ProcState {
  int pid = -1;
  bool running = false;
  int last_exit_code = 0;
};

class Supervisor {
public:
  // локальный режим (используется App::run)
  Supervisor(std::filesystem::path run_dir, std::filesystem::path logs_dir);
  // git-режим (CLI)
  explicit Supervisor(std::filesystem::path repo_workdir);
  ~Supervisor();

  // Старый API
  bool start(const Unit& u);
  bool reload(const Unit& u);
  bool stop();

  // Новый API для CLI/Manage
  bool open_repo(const std::string& url_or_path, const std::string& branch);
  bool sync_and_apply();           // reconcile: add/remove/changed
  bool start_all();                // старт всего каталога по топопорядку
  bool rollback_unit(const std::string& name, const std::string& commit);

  bool start(const std::string& name);
  bool stop(const std::string& name);
  bool reload(const std::string& name);

  std::unordered_map<std::string, ProcState> status() const;

private:
  // helpers
  std::filesystem::path resolve_unit_path_by_name(const std::string& name) const;
  std::vector<std::string> scan_service_names() const;
  bool spawn_unit(const Unit& u);
  bool stop_unit(const Unit& u, ProcState* out_state);

  // зависимости
  std::vector<Unit> load_units_by_names(const std::vector<std::string>& names) const;
  DepGraph build_dep_graph(const std::vector<Unit>& units) const;

  // monitors
  void monitor_exit_loop(Unit u);
  void health_loop(Unit u);

private:
  std::filesystem::path run_dir_;
  std::filesystem::path logs_dir_;
  std::filesystem::path repo_workdir_;

  // ВАЖНО: инициализируем через публичную фабрику, а не приватный ctor
  GitRepo repo_ = GitRepo::open_local(std::filesystem::current_path());
  bool repo_opened_ = false;
  std::string repo_branch_ = "main";

  std::atomic_bool stop_flag_{false};
  std::thread exit_thread_;
  std::thread health_thread_;

  std::unordered_map<std::string, ProcState> proc_state_;
  std::unordered_map<std::string, std::filesystem::path> last_unit_path_;
};

} // namespace gitproc
