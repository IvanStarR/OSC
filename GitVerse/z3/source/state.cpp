#include <gitproc/state.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace gitproc {

std::filesystem::path StateStore::pid_file(const std::string& name, const fs::path& run_dir){
  return run_dir / (name + ".pid");
}
std::filesystem::path StateStore::status_file(const std::string& name, const fs::path& run_dir){
  return run_dir / (name + ".status.json");
}

static void ensure_parent_dir(const fs::path& f){
  auto p = f.parent_path();
  if (!p.empty() && !fs::exists(p)) fs::create_directories(p);
}

bool StateStore::write_pid(const fs::path& f, int pid){
  ensure_parent_dir(f);
  std::ofstream o(f);
  if(!o) return false;
  o << pid;
  return static_cast<bool>(o);
}

std::optional<int> StateStore::read_pid(const fs::path& f){
  std::ifstream in(f); if(!in) return std::nullopt;
  int pid=0; in>>pid; if(!in) return std::nullopt; return pid;
}

void StateStore::write_status_json(const fs::path& f, int pid, int exit_code){
  ensure_parent_dir(f);
  std::ofstream o(f);
  if (o) o << "{\"pid\":"<<pid<<",\"exit_code\":"<<exit_code<<"}\n";
}

} // namespace gitproc
