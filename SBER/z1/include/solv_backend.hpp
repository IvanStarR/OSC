#pragma once
#include <string>

#include "graph.hpp"

struct SolvStats {
    size_t repos_loaded {0};
    size_t solvables_seen {0};
    size_t runtime_edges {0};
    size_t build_edges {0};
};

bool build_graphs_with_libsolv(const std::string& repoids_csv,// "" = все из /var/cache/dnf
                               const std::string& archs_csv,  // "x86_64,noarch" и т.п.
                               Graph& runtime,
                               Graph& build,
                               SolvStats& stats,
                               int threads// 0=авто
);
