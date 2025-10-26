#include "gitproc/cgroup.hpp"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace gitproc {

static bool path_exists(const std::string &p) {
  struct stat st {};
  return ::stat(p.c_str(), &st) == 0;
}

static bool write_file(const std::string &p, const std::string &v) {
  std::ofstream f(p);
  if (!f.is_open())
    return false;
  f << v;
  return f.good();
}

bool cgroup_attach_and_limit(pid_t pid, const std::string &name,
                             const CgroupLimits &lims) {
  const std::string root = "/sys/fs/cgroup";
  if (!path_exists(root))
    return false;

  const std::string base = root + "/gitproc";
  ::mkdir(base.c_str(), 0755);
  const std::string grp = base + "/" + name;
  ::mkdir(grp.c_str(), 0755);

  if (!write_file(grp + "/cgroup.procs", std::to_string(pid)))
    return false;

  if (lims.memory_max_bytes.has_value()) {
    if (!write_file(grp + "/memory.max",
                    std::to_string(*lims.memory_max_bytes))) {
    }
  }
  if (lims.cpu_quota.has_value()) {
    uint64_t period = 100000;
    double q = *lims.cpu_quota;
    if (q <= 0) {
      (void)write_file(grp + "/cpu.max", "max " + std::to_string(period));
    } else {
      uint64_t quota = static_cast<uint64_t>(period * (q / 100.0));
      if (quota == 0)
        quota = 1;
      (void)write_file(grp + "/cpu.max",
                       std::to_string(quota) + " " + std::to_string(period));
    }
  }
  return true;
}

} // namespace gitproc
