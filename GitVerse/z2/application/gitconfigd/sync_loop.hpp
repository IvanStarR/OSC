#pragma once
#include "router.hpp"
#include <thread>
#include <atomic>
#include <chrono>

namespace sync_ns {

class SyncLoop {
public:
  SyncLoop(router_ns::Router* r, int interval_sec);
  void start();
  void stop();
private:
  router_ns::Router* router_;
  int interval_;
  std::atomic<bool> running_{false};
  std::thread th_;
};

}
