#pragma once
#include <string>
#include <optional>
#include <cstdint>

namespace gitproc {

struct CgroupLimits {
    std::optional<uint64_t> memory_max_bytes; 
    std::optional<double> cpu_quota;          
};

bool cgroup_attach_and_limit(pid_t pid, const std::string& group_name, const CgroupLimits& lims);

} // namespace gitproc
