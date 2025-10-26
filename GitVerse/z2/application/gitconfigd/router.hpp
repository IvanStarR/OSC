#pragma once
#include "http.hpp"
#include "util.hpp"
#include "gitconfig/kv.hpp"
#include "gitconfig/conflict.hpp"
#include "gitconfig/nodes.hpp"
#include <string>

namespace router_ns {

struct Cfg {
  std::string repo;
  std::string data_root;
  std::string remote;
  std::string branch;
};

class Router {
public:
  Router(Cfg cfg);
  http_ns::Response handle(const http_ns::Request& r);
private:
  gitconfig::KVStore kv_;
  gitconfig::Repo repo_;
  Cfg cfg_;
  http_ns::Response handle_keys_get(const std::string& key);
  http_ns::Response handle_keys_post(const std::string& key, const std::string& body);
  http_ns::Response handle_keys_delete(const std::string& key);
  http_ns::Response handle_list(const std::string& query);
  http_ns::Response handle_sync();
  http_ns::Response handle_nodes_get();
  http_ns::Response handle_nodes_post(const std::string& body);
};

}
