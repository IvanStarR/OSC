#include <sysaudit/filter.hpp>

#include <algorithm>
#include <fstream>
#include <regex>

namespace sysaudit {

PathFilter::PathFilter(const std::filesystem::path& base_dir, std::vector<std::string> ignore_exts)
: base_(base_dir.lexically_normal()), ignore_exts_(std::move(ignore_exts)) {}

static bool ends_with(const std::string& s, const std::string& suf){
    if (s.size() < suf.size()) return false;
    return std::equal(suf.rbegin(), suf.rend(), s.rbegin());
}

std::string PathFilter::to_slash(const std::filesystem::path& p){
    auto s = p.lexically_normal().string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string PathFilter::trim(const std::string& s){
    size_t i=0, j=s.size();
    while (i<j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    while (j>i && std::isspace(static_cast<unsigned char>(s[j-1]))) --j;
    return s.substr(i, j-i);
}

std::string PathFilter::glob_to_regex(const std::string& pat, bool anchored, bool dir_only){
    std::string rx;
    if (anchored) rx += '^';
    size_t i=0;
    while (i<pat.size()){
        char c = pat[i];
        if (c == '*') {
            if ((i+1)<pat.size() && pat[i+1]=='*') {
                bool has_slash_after = (i+2<pat.size() && pat[i+2]=='/');
                rx += ".*";
                i += has_slash_after ? 3 : 2;
                continue;
            } else {
                rx += "[^/]*";
                ++i;
                continue;
            }
        } else if (c == '?') {
            rx += "[^/]";
        } else if (c == '.') {
            rx += "\\.";
        } else if (c == '+') {
            rx += "\\+";
        } else if (c == '(' || c==')' || c=='{' || c=='}' || c=='[' || c==']' || c=='^' || c=='$' || c=='|') {
            rx.push_back('\\'); rx.push_back(c);
        } else if (c == '/') {
            rx.push_back('/');
        } else {
            rx.push_back(c);
        }
        ++i;
    }
    if (dir_only) rx += "(/.*)?";
    if (anchored) rx += '$';
    return rx;
}

std::optional<PathFilter::Rule> PathFilter::compile_rule(const std::string& raw){
    std::string s = trim(raw);
    if (s.empty()) return std::nullopt;
    if (s[0] == '#') return std::nullopt;

    bool include = false;
    std::string pat = s;
    if (pat[0]=='!') { include = true; pat.erase(0,1); pat = trim(pat); if (pat.empty()) return std::nullopt; }

    bool dir_only = false;
    if (!pat.empty() && pat.back()=='/') { dir_only = true; pat.pop_back(); }

    bool anchored = false;
    if (!pat.empty() && pat.front()=='/') { anchored = true; pat.erase(0,1); }

    if (pat.empty()) return std::nullopt;

    Rule r;
    r.pattern = pat;
    r.include_rule = include;
    r.dir_only = dir_only;
    r.anchored = anchored;
    r.regex_str = glob_to_regex(pat, anchored, dir_only);
    r.valid = true;
    return r;
}

void PathFilter::add_pattern(const std::string& pattern, bool include_rule){
    std::string p = pattern;
    if (include_rule && !p.empty() && p[0] != '!') p = "!" + p;
    auto r = compile_rule(p);
    if (r) rules_.push_back(*r);
}

bool PathFilter::load_patterns_from_file(const std::filesystem::path& file){
    std::ifstream in(file);
    if (!in.good()) return false;
    std::string line;
    while (std::getline(in, line)) {
        auto r = compile_rule(line);
        if (r) rules_.push_back(*r);
    }
    return true;
}

bool PathFilter::is_ignored(const std::filesystem::path& p, [[maybe_unused]] bool is_dir) const {
    auto p_norm = p.lexically_normal();
    auto base_norm = base_.lexically_normal();

    std::string s_abs = to_slash(p_norm);
    std::string s_base = to_slash(base_norm);

    std::string rel;
    if (!s_base.empty()) {
        if (s_abs.rfind(s_base + "/", 0) == 0) {
            rel = s_abs.substr(s_base.size() + 1);
        } else if (s_abs == s_base) {
            rel.clear();
        } else {
            rel = s_abs;
        }
    } else {
        rel = s_abs;
    }

    for (const auto& part : std::filesystem::path(rel)) {
        if (part == ".git") return true;
    }

    for (const auto& ext : ignore_exts_) {
        if (ext == "~") {
            if (ends_with(rel, "~")) return true;
        } else {
            if (ends_with(rel, ext)) return true;
        }
    }

    bool match = false;
    for (const auto& r : rules_) {
        if (!r.valid) continue;
        try {
            std::regex re(r.regex_str);
            bool ok = std::regex_search(rel, re);
            if (ok) {
                match = !r.include_rule;
            }
        } catch (...) {
        }
    }
    return match;
}

} // namespace sysaudit
