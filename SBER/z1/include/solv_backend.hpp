#pragma once
#include <string>

#include "graph.hpp"

struct SolvStats {
    size_t repos_loaded {0};
    size_t solvables_seen {0};
    size_t runtime_edges {0};
    size_t build_edges {0};
};

bool build_graphs_with_libsolv(const std::string& repoids_csv,
                               const std::string& archs_csv,  
                               Graph& runtime,
                               Graph& build,
                               SolvStats& stats,
                               int threads
);
