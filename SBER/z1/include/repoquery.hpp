// repoquery.hpp
#pragma once
#include <string>
#include <vector>

#include "cache.hpp"
#include "config.hpp"
std::vector<std::string> list_all_binary_packages(const Config&);
std::vector<std::string> list_all_srpms(const Config&);
std::vector<std::string> resolve_runtime_requires(const Config&, const std::string& pkg);
std::vector<std::string> resolve_build_requires(const Config&, const std::string& srpm);
