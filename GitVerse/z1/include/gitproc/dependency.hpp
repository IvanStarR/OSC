#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>
#include <unordered_set>
#include <stdexcept>

namespace gitproc {

// Граф зависимостей: ребро A->B означает "A должен стартовать раньше B"
using DepGraph = std::unordered_map<std::string, std::vector<std::string>>;

// Топологическая сортировка (Kahn)
inline std::vector<std::string> topo_sort(const DepGraph& g){
  std::unordered_map<std::string,int> indeg;
  for (const auto& [svc, deps] : g) {
    indeg.try_emplace(svc, 0);
    for (const auto& to : deps) {
      indeg.try_emplace(to, 0);
    }
  }
  for (const auto& [svc, deps] : g) {
    for (const auto& to : deps) {
      indeg[to]++; // входы в "to"
    }
  }

  std::queue<std::string> q;
  for (const auto& [n,d] : indeg) if (d == 0) q.push(n);

  std::vector<std::string> out; out.reserve(indeg.size());
  while (!q.empty()){
    auto u = q.front(); q.pop();
    out.push_back(u);
    auto it = g.find(u);
    if (it == g.end()) continue;
    for (const auto& v : it->second){
      if (--indeg[v] == 0) q.push(v);
    }
  }
  if (out.size() != indeg.size()){
    throw std::runtime_error("dependency cycle detected");
  }
  return out;
}

} // namespace gitproc
