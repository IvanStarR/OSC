#include "gitconfig/repo.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <array>
#include <vector>

namespace gitconfig {

ExecResult run_sh(const std::string& cmd) {
  std::array<char, 4096> buf{};
  std::string out;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return {-1, ""};
  while (true) {
    size_t n = fread(buf.data(), 1, buf.size(), pipe);
    if (n == 0) break;
    out.append(buf.data(), n);
  }
  int rc = pclose(pipe);
  if (WIFEXITED(rc)) rc = WEXITSTATUS(rc);
  return {rc, out};
}

std::string shell_quote(const std::string& s) {
  std::string r = "'";
  for (char c : s) {
    if (c == '\'') r += "'\\''";
    else r += c;
  }
  r += "'";
  return r;
}

static bool stat_mode(const std::string& p, struct stat* st) {
  return ::stat(p.c_str(), st) == 0;
}

bool file_exists(const std::string& path) {
  struct stat st{};
  if (!stat_mode(path, &st)) return false;
  return S_ISREG(st.st_mode);
}

bool dir_exists(const std::string& path) {
  struct stat st{};
  if (!stat_mode(path, &st)) return false;
  return S_ISDIR(st.st_mode);
}

static bool mkdir_one(const std::string& p) {
  if (p.empty()) return false;
  if (dir_exists(p)) return true;
  return ::mkdir(p.c_str(), 0755) == 0;
}

static std::vector<std::string> split_path(const std::string& p) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : p) {
    if (c == '/') {
      if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
    } else cur.push_back(c);
  }
  if (!cur.empty()) parts.push_back(cur);
  return parts;
}

bool make_dirs(const std::string& p, std::string* err) {
  if (p.empty()) return true;
  if (dir_exists(p)) return true;
  std::string cur;
  if (p[0] == '/') cur = "/";
  auto parts = split_path(p);
  for (size_t i = 0; i < parts.size(); ++i) {
    if (!cur.empty() && cur.back() != '/') cur.push_back('/');
    cur += parts[i];
    if (!dir_exists(cur)) {
      if (!mkdir_one(cur)) {
        if (err) *err = "mkdir failed: " + cur;
        return false;
      }
    }
  }
  return true;
}

}
