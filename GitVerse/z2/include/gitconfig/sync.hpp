#pragma once
#include <string>
#include <atomic>
#include <thread>
#include "gitconfig/kv.hpp"
#include "gitconfig/repo.hpp"

namespace gitconfig {

class SyncLoop {
public:
  SyncLoop(KVStore& kv, Repo& repo, std::string remote, std::string branch, int interval_sec);
  void start();
  void stop();
private:
  void run();
  KVStore& kv_;
  Repo& repo_;
  std::string remote_;
  std::string branch_;
  int interval_sec_;
  std::atomic<bool> running_{false};
  std::thread th_;
};

}
