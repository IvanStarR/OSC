#include "server.hpp"
#include <asio.hpp>
#include <thread>
#include <atomic>
#include <sstream>

namespace server_ns {

struct Server::Impl {
  asio::io_context io;
  asio::ip::tcp::acceptor acc;
  Handler handler;
  std::atomic<bool> stopping{false};
  Impl(const std::string& addr, unsigned short port, Handler h)
    : io(), acc(io, asio::ip::tcp::endpoint(asio::ip::make_address(addr), port)), handler(std::move(h)) {}
};

static bool read_line(asio::ip::tcp::socket& sock, std::string& line) {
  line.clear();
  char c;
  for (;;) {
    asio::error_code ec;
    size_t n = sock.read_some(asio::buffer(&c,1), ec);
    if (ec) return false;
    if (n==1) {
      if (c=='\r') continue;
      if (c=='\n') break;
      line.push_back(c);
    }
  }
  return true;
}

static bool read_exact(asio::ip::tcp::socket& sock, std::string& data, size_t len) {
  data.clear(); data.resize(len);
  size_t got=0;
  asio::error_code ec;
  while (got<len) {
    size_t n = sock.read_some(asio::buffer(&data[got], len-got), ec);
    if (ec) return false;
    got += n;
  }
  return true;
}

static http_ns::Request parse_request(asio::ip::tcp::socket& sock) {
  http_ns::Request req;
  std::string line;
  if (!read_line(sock, line)) return req;
  std::istringstream rl(line);
  rl >> req.method;
  std::string url;
  rl >> url;
  size_t qpos = url.find('?');
  if (qpos==std::string::npos) req.path = url;
  else { req.path = url.substr(0,qpos); req.query = url.substr(qpos+1); }
  std::string proto; rl >> proto;

  for (;;) {
    if (!read_line(sock, line)) return req;
    if (line.empty()) break;
    size_t col = line.find(':');
    if (col!=std::string::npos) {
      std::string k = line.substr(0,col);
      while (col+1<line.size() && line[col+1]==' ') col++;
      std::string v = line.substr(col+1);
      req.headers[k] = v;
    }
  }
  auto it = req.headers.find("Content-Length");
  if (it != req.headers.end()) {
    size_t len = static_cast<size_t>(std::stoul(it->second));
    std::string body;
    if (read_exact(sock, body, len)) req.body = std::move(body);
  }
  return req;
}

static void write_response(asio::ip::tcp::socket& sock, const http_ns::Response& resp) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << resp.status << " " << http_ns::reason_phrase(resp.status) << "\r\n";
  bool has_ct = resp.headers.find("Content-Type") != resp.headers.end();
  bool has_cl = resp.headers.find("Content-Length") != resp.headers.end();
  for (auto& kv : resp.headers) ss << kv.first << ": " << kv.second << "\r\n";
  if (!has_ct) ss << "Content-Type: text/plain\r\n";
  if (!has_cl) ss << "Content-Length: " << resp.body.size() << "\r\n";
  ss << "Connection: close\r\n\r\n";
  ss << resp.body;
  auto s = ss.str();
  asio::error_code ec;
  asio::write(sock, asio::buffer(s.data(), s.size()), ec);
}

Server::Server(const std::string& addr, unsigned short port, Handler h)
  : impl_(std::make_unique<Impl>(addr, port, std::move(h))) {}

Server::~Server() = default;

void Server::run() {
  while (!impl_->stopping.load()) {
    asio::ip::tcp::socket sock(impl_->io);
    asio::error_code ec;
    impl_->acc.accept(sock, ec);
    if (ec) continue;
    auto req = parse_request(sock);
    auto resp = impl_->handler(req);
    write_response(sock, resp);
  }
}

void Server::stop() {
  impl_->stopping.store(true);
  asio::error_code ec;
  impl_->acc.close(ec);
}

}
