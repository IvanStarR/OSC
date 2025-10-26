#pragma once
#include <string>
#include <vector>
#include <optional>

namespace gitconfig {

class Repo;

class Nodes {
public:
  static std::string peers_rel(const std::string& data_root);
  static bool add_peer(const std::string& repo_path, const std::string& data_root, const std::string& url, std::string* err);
  static std::vector<std::string> list_peers(const std::string& repo_path, const std::string& data_root);
  static std::string remote_name_for(const std::string& url);
  static bool ensure_remote_for_peer(Repo& repo, const std::string& url, std::string* err);
};

}