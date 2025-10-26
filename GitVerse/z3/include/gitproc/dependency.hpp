#pragma once
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gitproc {

using DepGraph = std::unordered_map<std::string, std::vector<std::string>>;

inline std::vector<std::string> topo_sort(const DepGraph &g) {
  std::unordered_map<std::string, int> indeg;
  for (const auto &[svc, deps] : g) {
    indeg.try_emplace(svc, 0);
    for (const auto &to : deps) {
      indeg.try_emplace(to, 0);
    }
  }
  for (const auto &[svc, deps] : g) {
    for (const auto &to : deps) {
      indeg[to]++;
    }
  }

  std::queue<std::string> q;
  for (const auto &[n, d] : indeg)
    if (d == 0)
      q.push(n);

  std::vector<std::string> out;
  out.reserve(indeg.size());
  while (!q.empty()) {
    auto u = q.front();
    q.pop();
    out.push_back(u);
    auto it = g.find(u);
    if (it == g.end())
      continue;
    for (const auto &v : it->second) {
      if (--indeg[v] == 0)
        q.push(v);
    }
  }
  if (out.size() != indeg.size()) {
    throw std::runtime_error("dependency cycle detected");
  }
  return out;
}

} // namespace gitproc
