#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace sysaudit {

class PathFilter {
public:
    PathFilter(const std::filesystem::path& base_dir, std::vector<std::string> ignore_exts);

    void add_pattern(const std::string& pattern, bool include_rule);
    bool load_patterns_from_file(const std::filesystem::path& file);
    bool is_ignored(const std::filesystem::path& p, bool is_dir) const;

private:
    struct Rule {
        std::string pattern;
        bool include_rule{false};
        bool dir_only{false};
        bool anchored{false};
        bool valid{false};
        std::string regex_str;
    };

    std::filesystem::path base_;
    std::vector<std::string> ignore_exts_;
    std::vector<Rule> rules_;

    static std::string to_slash(const std::filesystem::path& p);
    static std::string trim(const std::string& s);
    static std::optional<Rule> compile_rule(const std::string& pat);
    static std::string glob_to_regex(const std::string& pat, bool anchored, bool dir_only);
};

} // namespace sysaudit
