#include "gitconfig/history.hpp"
#include "gitconfig/kv.hpp"
#include <vector>

namespace gitconfig {

std::vector<CommitEntry> History::list_for_key(const KVStore& kv, const std::string& key, int limit, std::string* err) {
  std::string rel = kv.key_to_relpath(key);
  auto pairs = kv.repo().log_path(rel, limit, err);
  std::vector<CommitEntry> out;
  for (auto& p : pairs) out.push_back({p.first, p.second});
  return out;
}

}
