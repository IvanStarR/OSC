#pragma once
#include <functional>
#include <filesystem>
#include <atomic>
#include <unordered_map>

namespace sysaudit {

enum class EventKind {
    Create,
    Modify,
    Delete,
    MoveFrom,
    MoveTo,
};

struct FileEvent {
    EventKind kind;
    std::filesystem::path path;
    bool is_dir{false};
};

const char* to_string(EventKind k);

class DirWatcher {
public:
    explicit DirWatcher(std::filesystem::path dir);
    ~DirWatcher();

    bool open_recursive();
    void close();

    void run_loop(std::atomic<bool>& stop_flag,
                  const std::function<void(const FileEvent&)>& on_event,
                  const std::function<void()>& on_tick = {});

    size_t watch_count() const;

private:
    std::filesystem::path dir_;
    int inotify_fd_{-1};

    std::unordered_map<int, std::filesystem::path> wd_to_path_;
    std::unordered_map<std::string, int> path_to_wd_;

    bool add_watch(const std::filesystem::path& p);
    void remove_watch(const std::filesystem::path& p);
    void add_tree(const std::filesystem::path& root);
    std::filesystem::path base_for_wd(int wd) const;
};

} // namespace sysaudit
