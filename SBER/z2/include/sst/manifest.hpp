#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace uringkv {

// CURRENT содержит текст с последним номером SST, например: "42\n"
bool read_current(const std::string& sst_dir, uint64_t& last_index);
bool write_current_atomic(const std::string& sst_dir, uint64_t last_index);

// Вернуть список *.sst файлов по возрастанию индекса (000001.sst, ...)
std::vector<std::string> list_sst_sorted(const std::string& sst_dir);

std::string sst_name(uint64_t index); // "000001.sst"

} // namespace uringkv
