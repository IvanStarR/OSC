#include "repoquery.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>
#include <cctype> 
#include "exec.hpp"

static std::string strip_to_name(const std::string& nevra) {
    for (size_t i = 0; i < nevra.size(); ++i)
        if (nevra[i] == '-' && i + 1 < nevra.size() && isdigit((unsigned char)nevra[i + 1]))
            return nevra.substr(0, i);
    return nevra;
}

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur)) {
        if (!cur.empty())
            v.push_back(cur);
    }
    return v;
}

std::vector<std::string> list_all_binary_packages(const Config& cfg) {
    std::string cmd = "dnf repoquery --available --qf '%{name}'";
    if (!cfg.repoids.empty())
        cmd += " --repoid '" + cfg.repoids + "'";
    if (!cfg.archs.empty())
        cmd += " --arch '" + cfg.archs + "'";
    auto r = run_cmd(cmd);
    auto lines = split_lines(r.out);
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    return lines;
}

std::vector<std::string> list_all_srpms(const Config& cfg) {
    std::string cmd = "dnf repoquery --available --qf '%{sourcerpm}'";
    if (!cfg.repoids.empty())
        cmd += " --repoid '" + cfg.repoids + "'";
    if (!cfg.archs.empty())
        cmd += " --arch '" + cfg.archs + "'";
    auto r = run_cmd(cmd);
    auto lines = split_lines(r.out);
    std::vector<std::string> names;
    names.reserve(lines.size());
    for (auto& x : lines) {
        size_t p = x.rfind('-');
        if (p != std::string::npos && p + 1 < x.size() && isdigit((unsigned char)x[p + 1]))
            names.push_back(x.substr(0, p));
        else {
            const std::string suf = ".src.rpm";
            if (x.size() > suf.size() && x.rfind(suf) == x.size() - suf.size())
                names.push_back(x.substr(0, x.size() - suf.size()));
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::vector<std::string> resolve_runtime_requires(const Config& cfg, const std::string& pkg) {
    std::vector<std::string> cached;
    if (cache_load(cfg.cachedir, "runtime:" + pkg, cached))
        return cached;

    std::string cmd = "dnf repoquery --requires --resolve '" + pkg + "'";
    if (!cfg.repoids.empty())
        cmd += " --repoid '" + cfg.repoids + "'";
    if (!cfg.archs.empty())
        cmd += " --arch '" + cfg.archs + "'";
    auto r = run_cmd(cmd);
    auto lines = split_lines(r.out);
    std::vector<std::string> names;
    names.reserve(lines.size());
    for (auto& l : lines)
        names.push_back(strip_to_name(l));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    cache_store(cfg.cachedir, "runtime:" + pkg, names);
    return names;
}

std::vector<std::string> resolve_build_requires(const Config& cfg, const std::string& srpm) {
    std::vector<std::string> cached;
    if (cache_load(cfg.cachedir, "build:" + srpm, cached))
        return cached;

    std::string cmd = "dnf repoquery --requires --resolve --srpm '" + srpm + "'";
    if (!cfg.repoids.empty())
        cmd += " --repoid '" + cfg.repoids + "'";
    auto r = run_cmd(cmd);
    auto lines = split_lines(r.out);
    std::vector<std::string> names;
    names.reserve(lines.size());
    for (auto& l : lines)
        names.push_back(strip_to_name(l));
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    cache_store(cfg.cachedir, "build:" + srpm, names);
    return names;
}