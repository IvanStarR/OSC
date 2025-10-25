#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace uringkv {

bool read_current(const std::string& sst_dir, uint64_t& last_index);
bool write_current_atomic(const std::string& sst_dir, uint64_t last_index);

std::vector<std::string> list_sst_sorted(const std::string& sst_dir);

std::string sst_name(uint64_t index); // "000001.sst"

} // namespace uringkv
