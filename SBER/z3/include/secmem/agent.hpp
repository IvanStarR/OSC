#pragma once
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <unordered_set>
#include "secmem/storage.hpp"

namespace secmem {
struct AgentConfig {
  std::string socket_path{"/run/secmem.sock"};
  std::unordered_set<uint32_t> allowed_uids;
  std::unordered_set<uint32_t> allowed_gids;
  std::chrono::seconds default_ttl{600};
};

class Agent {
public:
  explicit Agent(AgentConfig cfg);
  ~Agent();
  int run();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
