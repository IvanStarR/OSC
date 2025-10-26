#pragma once
#include "http.hpp"
#include <functional>
#include <string>
#include <memory>

namespace server_ns {

class Server {
public:
  using Handler = std::function<http_ns::Response(const http_ns::Request&)>;
  Server(const std::string& addr, unsigned short port, Handler h);
  ~Server();
  void run();
  void stop();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
