#pragma once
#include <string>
#include "gitconfig/http.hpp"
#include "gitconfig/kv.hpp"
#include "gitconfig/conflict.hpp"

namespace gitconfig {

class Router {
public:
  Router(KVStore& kv, Repo& repo, std::string remote, std::string branch);
  HttpResponse handle(const HttpRequest& req);
private:
  KVStore& kv_;
  Repo& repo_;
  std::string remote_;
  std::string branch_;
  bool parse_bool(const std::string& v) const;
  static std::string url_decode(const std::string& s);
};

}
