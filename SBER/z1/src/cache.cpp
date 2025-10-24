#include "cache.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

static std::string sanitize(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == ':'))
            c = '_';
    return r;
}
bool cache_load(const std::string& dir, const std::string& key, std::vector<std::string>& out) {
    std::filesystem::create_directories(dir);
    std::ifstream f(dir + "/" + sanitize(key) + ".txt");
    if (!f)
        return false;
    std::string line;
    out.clear();
    while (std::getline(f, line)) {
        if (!line.empty())
            out.push_back(line);
    }
    return true;
}
void cache_store(const std::string& dir, const std::string& key, const std::vector<std::string>& vals) {
    std::filesystem::create_directories(dir);
    std::ofstream f(dir + "/" + sanitize(key) + ".txt", std::ios::trunc);
    for (auto& v : vals)
        f << v << "\n";
}