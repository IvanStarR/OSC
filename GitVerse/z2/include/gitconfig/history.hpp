#pragma once
#include <string>
#include <vector>
#include <optional>

namespace gitconfig {

struct CommitEntry {
  std::string sha;
  long long unix_ts;
};

class History {
public:
  static std::vector<CommitEntry> list_for_key(const class KVStore& kv, const std::string& key, int limit, std::string* err);
};

}
