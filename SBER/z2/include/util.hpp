#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace uringkv {
bool ensure_dir(const std::string& p);
std::string join_path(std::string a, std::string b);
uint64_t dummy_checksum(std::string_view a, std::string_view b);
} // namespace uringkv
