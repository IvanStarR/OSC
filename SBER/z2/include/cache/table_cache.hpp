#pragma once
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "uringkv/sst/table.hpp"

namespace uringkv {

class TableCache {
public:
  explicit TableCache(size_t capacity_files = 64)
      : cap_(capacity_files ? capacity_files : 1) {}

  std::shared_ptr<SstTable> get_table(const std::string &path) {
    auto it = map_.find(path);
    if (it != map_.end()) {
      hits_++;
      lru_.splice(lru_.begin(), lru_, it->second);
      return it->second->table;
    }
    misses_++;
    auto tbl = std::make_shared<SstTable>(path);
    if (!tbl->good())
      return nullptr;
    opens_++;

    lru_.push_front({path, tbl});
    map_[path] = lru_.begin();

    // вытеснить, если нужно
    if (lru_.size() > cap_) {
      auto &back = lru_.back();
      map_.erase(back.path);
      lru_.pop_back();
    }
    return tbl;
  }

  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }
  uint64_t opens() const { return opens_; }
  void reset_stats() { hits_ = misses_ = opens_ = 0; }

private:
  struct Node {
    std::string path;
    std::shared_ptr<SstTable> table;
  };

  size_t cap_;
  std::list<Node> lru_;
  std::unordered_map<std::string, std::list<Node>::iterator> map_;

  uint64_t hits_ = 0, misses_ = 0, opens_ = 0;
};

} // namespace uringkv
