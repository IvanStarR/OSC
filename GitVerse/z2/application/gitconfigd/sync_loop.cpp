#include "sync_loop.hpp"

namespace sync_ns {

SyncLoop::SyncLoop(router_ns::Router* r, int interval_sec) : router_(r), interval_(interval_sec) {}

void SyncLoop::start() {
  if (interval_<=0) return;
  running_.store(true);
  th_ = std::thread([this](){
    while (running_.load()) {
      http_ns::Request req;
      req.method="POST"; req.path="/sync";
      router_->handle(req);
      for (int i=0;i<interval_ && running_.load();++i) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });
}

void SyncLoop::stop() {
  running_.store(false);
  if (th_.joinable()) th_.join();
}

}
