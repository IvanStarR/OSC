#include <sysaudit/repo.hpp>
#include <sysaudit/util.hpp>

#include <spdlog/spdlog.h>
#include <fmt/format.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace sysaudit {

GitRepo::GitRepo(std::filesystem::path root) : root_(std::move(root)) {}

bool GitRepo::is_repo() const {
    return std::filesystem::exists(root_ / ".git");
}

bool GitRepo::ensure_initialized() {
    if (is_repo()) return true;

    spdlog::info("Initializing git repo at {}", root_.string());
    int rc = 0;
    std::string out, err;

    if (!run_git({"git", "init"}, rc, &out, &err) || rc != 0) {
        spdlog::error("git init failed: rc={} err={}", rc, err);
        return false;
    }

    run_git({"git", "config", "user.name", "sysaudit"}, rc);
    run_git({"git", "config", "user.email", "sysaudit@local"}, rc);

    return true;
}

bool GitRepo::run_git(const std::vector<std::string>& args, int& rc,
                      std::string* out, std::string* err) const
{
    auto r = run_command(args, root_);
    rc = r.exit_code;
    if (out) *out = std::move(r.out);
    if (err) *err = std::move(r.err);
    return (rc == 0);
}

bool GitRepo::run_git_retry(const std::vector<std::string>& args, int& rc, int max_retries, int base_sleep_ms) const {
    std::string out, err;
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        run_git(args, rc, &out, &err);
        if (rc == 0) return true;
        bool lock = (err.find("index.lock") != std::string::npos) ||
                    (err.find("Another git process") != std::string::npos) ||
                    (err.find("Unable to create") != std::string::npos);
        if (!lock || attempt == max_retries) {
            if (rc != 0) spdlog::debug("git cmd failed rc={} err={}", rc, err);
            return false;
        }
        int sleep_ms = base_sleep_ms << attempt;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    return false;
}

bool GitRepo::add_and_commit(const std::filesystem::path& file, const std::string& message) {
    int rc = 0;
    std::error_code ec;
    auto rel = std::filesystem::weakly_canonical(file, ec);
    if (ec) rel = file;
    std::string rels;
    std::filesystem::path base = std::filesystem::weakly_canonical(root_, ec);
    if (!ec && rel.string().rfind(base.string(), 0) == 0) {
        rels = std::filesystem::relative(rel, base, ec).string();
        if (ec) rels = file.string();
    } else {
        rels = file.string();
    }

    if (!run_git_retry({"git", "add", "--", rels}, rc, 5, 20) || rc != 0) {
        spdlog::warn("git add failed for {}", rels);
        return false;
    }
    std::string out, err;
    run_git({"git", "diff", "--cached", "--quiet"}, rc, &out, &err);
    if (rc == 1) {
        if (!run_git_retry({"git", "commit", "-m", message}, rc, 5, 20) || rc != 0) {
            spdlog::warn("git commit failed for {}", rels);
            return false;
        }
    }
    return true;
}

bool GitRepo::remove_and_commit(const std::filesystem::path& file, const std::string& message) {
    int rc = 0;
    std::error_code ec;
    auto rel = std::filesystem::weakly_canonical(file, ec);
    if (ec) rel = file;
    std::string rels;
    std::filesystem::path base = std::filesystem::weakly_canonical(root_, ec);
    if (!ec && rel.string().rfind(base.string(), 0) == 0) {
        rels = std::filesystem::relative(rel, base, ec).string();
        if (ec) rels = file.string();
    } else {
        rels = file.string();
    }

    std::string out, err;
    if (!run_git_retry({"git", "rm", "-f", "--", rels}, rc, 5, 20)) {
        spdlog::debug("git rm warn rc={} err={}", rc, err);
    }

    run_git({"git", "diff", "--cached", "--quiet"}, rc, &out, &err);
    if (rc == 1) {
        if (!run_git_retry({"git", "commit", "-m", message}, rc, 5, 20) || rc != 0) {
            spdlog::warn("git commit (remove) failed for {}", rels);
            return false;
        }
    }
    return true;
}

bool GitRepo::add_all_and_commit(const std::string& message, int& rc_out) {
    int rc = 0;
    if (!run_git_retry({"git", "add", "-A"}, rc, 5, 20) || rc != 0) {
        rc_out = rc;
        return false;
    }
    std::string out, err;
    run_git({"git", "diff", "--cached", "--quiet"}, rc, &out, &err);
    if (rc == 1) {
        if (!run_git_retry({"git", "commit", "-m", message}, rc, 5, 20) || rc != 0) {
            rc_out = rc;
            return false;
        }
    }
    rc_out = 0;
    return true;
}

void GitRepo::ensure_default_gitignore() {
    auto gi = root_ / ".gitignore";
    if (std::filesystem::exists(gi)) return;

    std::ofstream f(gi, std::ios::out | std::ios::trunc);
    if (!f.good()) {
        spdlog::warn("cannot create .gitignore at {}", gi.string());
        return;
    }
    f << "*.tmp\n";
    f << "*.swp\n";
    f << "*~\n";
    f << "*.log\n";
    f.close();

    int rc = 0;
    if (run_git({"git", "add", "--", ".gitignore"}, rc) && rc == 0) {
        run_git({"git", "commit", "-m", "sysaudit: add default .gitignore"}, rc);
    }
}

} // namespace sysaudit
