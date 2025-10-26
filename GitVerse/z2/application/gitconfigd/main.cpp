#include "server.hpp"
#include "router.hpp"
#include "sync_loop.hpp"
#include "gitconfig/kv.hpp"
#include <string>
#include <iostream>
#include <cstdlib>

namespace gitconfigd_ns {

struct Args {
  std::string addr = "0.0.0.0";
  unsigned short port = 8080;
  std::string repo = ".";
  std::string data_root = "config";
  std::string remote = "origin";
  std::string branch = "main";
  int sync_interval = 0;
};

static Args parse(int argc, char** argv) {
  Args a;
  for (int i=1;i<argc;++i) {
    std::string s = argv[i];
    if (s=="--addr" && i+1<argc) a.addr = argv[++i];
    else if (s=="--port" && i+1<argc) a.port = static_cast<unsigned short>(std::stoi(argv[++i]));
    else if (s=="--repo" && i+1<argc) a.repo = argv[++i];
    else if (s=="--data-root" && i+1<argc) a.data_root = argv[++i];
    else if (s=="--remote" && i+1<argc) a.remote = argv[++i];
    else if (s=="--branch" && i+1<argc) a.branch = argv[++i];
    else if (s=="--sync-interval" && i+1<argc) a.sync_interval = std::stoi(argv[++i]);
  }
  return a;
}

}

int main(int argc, char** argv) {
  using namespace gitconfigd_ns;
  auto a = parse(argc, argv);

  router_ns::Cfg cfg{a.repo, a.data_root, a.remote, a.branch};
  router_ns::Router router(cfg);

  server_ns::Server srv(a.addr, a.port, [&](const http_ns::Request& r){ return router.handle(r); });

  sync_ns::SyncLoop loop(&router, a.sync_interval);
  loop.start();
  srv.run();
  loop.stop();
  return 0;
}
