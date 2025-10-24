#include "graph.hpp"
void Graph::add_node(const std::string& id, const std::string& kind) {
    std::lock_guard<std::mutex> lk(m_nodes);
    auto it = nodes.find(id);
    if (it == nodes.end())
        nodes.emplace(id, kind);
}
void Graph::add_edge(const std::string& src, const std::string& dst) {
    std::lock_guard<std::mutex> lk(m_edges);
    edges.emplace_back(src, dst);
}