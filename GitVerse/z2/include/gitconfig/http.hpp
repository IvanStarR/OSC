#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <vector>

namespace gitconfig {

struct HttpRequest {
  std::string method;
  std::string target;
  std::string path;
  std::string query;
  std::string body;
  std::vector<std::pair<std::string,std::string>> headers;
};

struct HttpResponse {
  int status = 200;
  std::string status_text = "OK";
  std::string content_type = "text/plain; charset=utf-8";
  std::string body;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
  HttpServer(std::string bind, int port, HttpHandler handler);
  bool start(std::string* err);
  void stop();
private:
  std::string bind_;
  int port_;
  HttpHandler handler_;
  std::atomic<bool> running_{false};
  int listen_fd_ = -1;
  std::thread accept_thread_;
};

}
