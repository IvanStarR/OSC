#pragma once
#include <string>

namespace gitconfig {

class KVStore;
class Repo;

struct SyncOutcome {
  bool ok = false;
  bool merged = false;
  bool conflicts_resolved = false;
  std::string remote;
  std::string error;
};

struct ConflictResolver {
  static SyncOutcome sync_lww(KVStore& kv, Repo& repo, const std::string& remote, const std::string& branch, std::string* err);
};

}
