#include "gitconfig/nodes.hpp"
#include "gitconfig/kv.hpp"
#include "gitconfig/repo.hpp"
#include <xxhash.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace gitconfig {

std::string Nodes::peers_rel(const std::string& data_root) {
  return data_root + "/_cluster/peers";
}

static std::string peers_abs(const std::string& repo_path, const std::string& data_root) {
  return std::filesystem::path(repo_path) / Nodes::peers_rel(data_root);
}

std::string Nodes::remote_name_for(const std::string& url) {
  auto h = XXH64(url.data(), url.size(), 0);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "peer-%016llx", static_cast<unsigned long long>(h));
  return std::string(buf);
}

bool Nodes::add_peer(const std::string& repo_path, const std::string& data_root, const std::string& url, std::string* err) {
  auto path = peers_abs(repo_path, data_root);
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
      if (line == url) return true;
    }
  }
  std::ofstream out(path, std::ios::app);
  if (!out.is_open()) { if (err) *err = "open peers failed"; return false; }
  out << url << "\n";
  return true;
}

std::vector<std::string> Nodes::list_peers(const std::string& repo_path, const std::string& data_root) {
  std::vector<std::string> v;
  auto path = peers_abs(repo_path, data_root);
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty()) v.push_back(line);
  }
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

bool Nodes::ensure_remote_for_peer(Repo& repo, const std::string& url, std::string* err) {
  auto name = remote_name_for(url);
  return repo.set_remote(name, url, err);
}

}
