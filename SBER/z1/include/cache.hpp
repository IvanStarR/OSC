#pragma once
#include <string>
#include <vector>
bool cache_load(const std::string& cachedir, const std::string& key, std::vector<std::string>& out);
void cache_store(const std::string& cachedir, const std::string& key, const std::vector<std::string>& vals);
