#include <sysaudit/watcher.hpp>
#include <spdlog/spdlog.h>

#include <sys/inotify.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>

namespace sysaudit {

const char* to_string(EventKind k){
    switch (k){
        case EventKind::Create:   return "create";
        case EventKind::Modify:   return "modify";
        case EventKind::Delete:   return "delete";
        case EventKind::MoveFrom: return "move_from";
        case EventKind::MoveTo:   return "move_to";
    }
    return "unknown";
}

DirWatcher::DirWatcher(std::filesystem::path dir)
: dir_(std::move(dir))
{}

DirWatcher::~DirWatcher(){
    close();
}

bool DirWatcher::add_watch(const std::filesystem::path& p){
    if (!std::filesystem::is_directory(p)) return false;
    if (p.filename() == ".git") return false;
    std::error_code ec;
    auto can = std::filesystem::weakly_canonical(p, ec);
    std::string key = ec ? p.string() : can.string();
    if (path_to_wd_.count(key)) return true;

    int wd = ::inotify_add_watch(inotify_fd_, key.c_str(),
        IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR);
    if (wd < 0) {
        spdlog::warn("inotify_add_watch failed for {}: {}", key, std::strerror(errno));
        return false;
    }
    wd_to_path_[wd] = key;
    path_to_wd_[key] = wd;
    spdlog::debug("watch added: {} (wd={})", key, wd);
    return true;
}

void DirWatcher::remove_watch(const std::filesystem::path& p){
    std::error_code ec;
    auto can = std::filesystem::weakly_canonical(p, ec);
    std::string key = ec ? p.string() : can.string();
    auto it = path_to_wd_.find(key);
    if (it == path_to_wd_.end()) return;
    int wd = it->second;
    ::inotify_rm_watch(inotify_fd_, wd);
    wd_to_path_.erase(wd);
    path_to_wd_.erase(it);
    spdlog::debug("watch removed: {} (wd={})", key, wd);
}

void DirWatcher::add_tree(const std::filesystem::path& root){
    if (root.filename() != ".git") add_watch(root);
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it)
    {
        if (ec) break;
        if (it->path().filename() == ".git") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(ec)) add_watch(it->path());
    }
}

std::filesystem::path DirWatcher::base_for_wd(int wd) const{
    auto it = wd_to_path_.find(wd);
    if (it == wd_to_path_.end()) return dir_;
    return it->second;
}

bool DirWatcher::open_recursive(){
    inotify_fd_ = ::inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        spdlog::error("inotify_init1 failed: {}", std::strerror(errno));
        return false;
    }
    add_tree(dir_);
    return true;
}

void DirWatcher::close(){
    if (inotify_fd_ >= 0) {
        for (auto& kv : wd_to_path_) {
            ::inotify_rm_watch(inotify_fd_, kv.first);
        }
        wd_to_path_.clear();
        path_to_wd_.clear();
        ::close(inotify_fd_);
        inotify_fd_ = -1;
    }
}

size_t DirWatcher::watch_count() const {
    return path_to_wd_.size();
}

void DirWatcher::run_loop(std::atomic<bool>& stop_flag,
                          const std::function<void(const FileEvent&)>& on_event,
                          const std::function<void()>& on_tick)
{
    std::array<char, 32 * 1024> buf{};

    while (!stop_flag.load()) {
        ssize_t n = ::read(inotify_fd_, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (on_tick) on_tick();
                ::usleep(100 * 1000);
                continue;
            } else {
                spdlog::error("inotify read error: {}", std::strerror(errno));
                break;
            }
        }
        ssize_t off = 0;
        while (off < n) {
            auto* ev = reinterpret_cast<inotify_event*>(buf.data() + off);
            off += sizeof(inotify_event) + ev->len;

            if (ev->len == 0) continue;
            if (ev->name[0] == '\0') continue;

            std::filesystem::path base = base_for_wd(ev->wd);
            std::filesystem::path p = base / ev->name;
            bool is_dir = ev->mask & IN_ISDIR;

            if (is_dir && (ev->mask & IN_CREATE)) {
                if (p.filename() != ".git") add_watch(p);
            }
            if (is_dir && (ev->mask & IN_MOVED_TO)) {
                if (p.filename() != ".git") add_watch(p);
            }
            if (is_dir && (ev->mask & IN_DELETE)) {
                remove_watch(p);
            }
            if (is_dir && (ev->mask & IN_MOVED_FROM)) {
                remove_watch(p);
            }

            if (ev->mask & IN_CREATE)      on_event(FileEvent{EventKind::Create,   p, is_dir});
            if (ev->mask & IN_CLOSE_WRITE) on_event(FileEvent{EventKind::Modify,   p, is_dir});
            if (ev->mask & IN_MODIFY)      on_event(FileEvent{EventKind::Modify,   p, is_dir});
            if (ev->mask & IN_DELETE)      on_event(FileEvent{EventKind::Delete,   p, is_dir});
            if (ev->mask & IN_MOVED_FROM)  on_event(FileEvent{EventKind::MoveFrom, p, is_dir});
            if (ev->mask & IN_MOVED_TO)    on_event(FileEvent{EventKind::MoveTo,   p, is_dir});
        }
        if (on_tick) on_tick();
    }
}

} // namespace sysaudit
