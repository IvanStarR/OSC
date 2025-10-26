#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace sysaudit {

struct CmdResult {
    int exit_code{};
    std::string out;
    std::string err;
};

CmdResult run_command(const std::vector<std::string>& argv,
                      const std::filesystem::path& cwd);

std::string iso8601_now();

} // namespace sysaudit
