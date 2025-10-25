#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <optional>

namespace gitproc {

struct ServiceEntry {
    std::string name;   // без расширения, например: "web"
    std::string path;   // полный путь к файлу *.service
};

class GitRepo {
public:
    GitRepo(std::string workdir);
    bool open_or_clone(const std::string& url_or_path, const std::string& branch);
    bool pull_reset(); // fetch + reset --hard
    std::vector<ServiceEntry> scan_services(const std::string& rel_dir = "services") const;
    std::optional<std::string> read_file(const std::string& rel_path) const; // helper

    // rollback конкретного unit-файла к коммиту (и оставить HEAD на текущей ветке)
    bool checkout_file_at(const std::string& commit, const std::string& rel_file);

    const std::string& workdir() const { return workdir_; }

private:
    std::string workdir_;
    std::string branch_;
    bool is_bare_path_{false};
};

struct ServicesDiff {
    std::unordered_set<std::string> added;   // имена сервисов
    std::unordered_set<std::string> removed;
    std::unordered_set<std::string> changed;
};

// сравнить предыдущий набор сервисов и текущий, на основе имени
ServicesDiff diff_sets(const std::vector<ServiceEntry>& old_list,
                       const std::vector<ServiceEntry>& new_list);

} // namespace gitproc
