#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace sysaudit {

class GitRepo {
public:
    explicit GitRepo(std::filesystem::path root);

    bool ensure_initialized();
    bool add_and_commit(const std::filesystem::path& file, const std::string& message);
    bool remove_and_commit(const std::filesystem::path& file, const std::string& message);

    bool add_all_and_commit(const std::string& message, int& rc_out);
    void ensure_default_gitignore();

private:
    std::filesystem::path root_;

    bool is_repo() const;
    bool run_git(const std::vector<std::string>& args, int& rc, std::string* out=nullptr, std::string* err=nullptr) const;
    bool run_git_retry(const std::vector<std::string>& args, int& rc, int max_retries, int base_sleep_ms) const;
};

} // namespace sysaudit
