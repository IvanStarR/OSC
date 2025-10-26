#pragma once
#include <string>

namespace gitconfig {

struct ExecResult {
  int code;
  std::string out;
};

ExecResult run_sh(const std::string& cmd);
std::string shell_quote(const std::string& s);
bool file_exists(const std::string& path);
bool dir_exists(const std::string& path);
bool make_dirs(const std::string& path, std::string* err);

}
