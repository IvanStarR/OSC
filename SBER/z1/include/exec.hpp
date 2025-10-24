#pragma once
#include <string>
struct CmdResult {
    int code;
    std::string out;
};
CmdResult run_cmd(const std::string& cmd);