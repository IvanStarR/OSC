#pragma once
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Graph {
    std::unordered_map<std::string, std::string> nodes;
    std::vector<std::pair<std::string, std::string>> edges;
    std::mutex m_nodes, m_edges;
    void add_node(const std::string& id, const std::string& kind);
    void add_edge(const std::string& src, const std::string& dst);
};