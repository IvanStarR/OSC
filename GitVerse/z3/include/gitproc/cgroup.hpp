#pragma once
#include <string>
#include <optional>
#include <cstdint>

namespace gitproc {

// Простейшая поддержка cgroup v2: memory.max и cpu.max
struct CgroupLimits {
    std::optional<uint64_t> memory_max_bytes; // MemoryLimit
    std::optional<double> cpu_quota;          // CPUQuota в процентах, 0..100 (или >100)
};

bool cgroup_attach_and_limit(pid_t pid, const std::string& group_name, const CgroupLimits& lims);
// Возвращает false, если не удалось (например, v2 не примонтирован). В этом случае продолжаем без лимитов.

} // namespace gitproc
