#include <sysaudit/watcher.hpp>
#include <sysaudit/repo.hpp>
#include <sysaudit/filter.hpp>
#include <sysaudit/util.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <fmt/format.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <fstream>
#include <memory>

using namespace sysaudit;

static std::atomic<bool> g_stop{false};
static std::atomic<bool> g_reload{false};

static void on_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_stop.store(true);
    } else if (sig == SIGHUP) {
        g_reload.store(true);
    }
}

struct Cli {
    std::filesystem::path watch_dir;
    std::vector<std::string> ignore_exts;
    std::vector<std::string> excludes;
    std::vector<std::string> includes;
    std::optional<std::filesystem::path> ignore_file;
    bool verbose = false;
    bool stats = false;
    int stats_interval_sec = 5;
    int debounce_ms = 200;
    std::optional<int> batch_count;
    std::optional<int> batch_window_ms;
    bool initial_snapshot = false;
    std::optional<std::filesystem::path> log_file;
    size_t log_rotate_max = 10 * 1024 * 1024;
    size_t log_rotate_files = 3;
};

static void print_usage(const char* argv0){
    fmt::print(
        "Usage:\n"
        "  {} --watch <DIR>\n"
        "     [--ignore-ext \".tmp,.swp,.log,~\"]\n"
        "     [--exclude PATTERN ...] [--include PATTERN ...] [--ignore-file PATH]\n"
        "     [--debounce-ms N]\n"
        "     [--batch-count N] [--batch-window-ms N]\n"
        "     [--stats [SEC]]\n"
        "     [--initial-snapshot]\n"
        "     [--log-file PATH] [--log-rotate-max BYTES] [--log-rotate-files N]\n"
        "     [--verbose]\n"
        "\n", argv0);
}

static std::vector<std::string> split_csv(std::string s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else if (!std::isspace(static_cast<unsigned char>(c))) { cur.push_back(c); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::optional<Cli> parse_cli(int argc, char** argv){
    Cli cli;
    for (int i=1;i<argc;i++){
        std::string a = argv[i];
        if ((a=="--watch" || a=="-w") && (i+1<argc)) {
            cli.watch_dir = argv[++i];
        } else if (a=="--ignore-ext" && (i+1<argc)) {
            cli.ignore_exts = split_csv(argv[++i]);
        } else if (a=="--exclude" && (i+1<argc)) {
            cli.excludes.push_back(argv[++i]);
        } else if (a=="--include" && (i+1<argc)) {
            cli.includes.push_back(argv[++i]);
        } else if (a=="--ignore-file" && (i+1<argc)) {
            cli.ignore_file = std::filesystem::path(argv[++i]);
        } else if (a=="--debounce-ms" && (i+1<argc)) {
            cli.debounce_ms = std::max(0, std::atoi(argv[++i]));
        } else if (a=="--batch-count" && (i+1<argc)) {
            cli.batch_count = std::max(1, std::atoi(argv[++i]));
        } else if (a=="--batch-window-ms" && (i+1<argc)) {
            cli.batch_window_ms = std::max(1, std::atoi(argv[++i]));
        } else if (a=="--stats") {
            cli.stats = true;
            if (i+1<argc && std::isdigit(static_cast<unsigned char>(argv[i+1][0]))) {
                cli.stats_interval_sec = std::max(1, std::atoi(argv[++i]));
            }
        } else if (a=="--initial-snapshot") {
            cli.initial_snapshot = true;
        } else if (a=="--log-file" && (i+1<argc)) {
            cli.log_file = std::filesystem::path(argv[++i]);
        } else if (a=="--log-rotate-max" && (i+1<argc)) {
            cli.log_rotate_max = static_cast<size_t>(std::stoll(argv[++i]));
        } else if (a=="--log-rotate-files" && (i+1<argc)) {
            cli.log_rotate_files = static_cast<size_t>(std::stoll(argv[++i]));
        } else if (a=="--verbose" || a=="-v") {
            cli.verbose = true;
        } else if (a=="--help" || a=="-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            spdlog::error("Unknown argument: {}", a);
            print_usage(argv[0]);
            return std::nullopt;
        }
    }
    if (cli.watch_dir.empty()) {
        spdlog::error("--watch <DIR> is required");
        print_usage(argv[0]);
        return std::nullopt;
    }
    if (cli.ignore_exts.empty()) {
        cli.ignore_exts = {".tmp", ".swp", ".log", "~"};
    }
    return cli;
}

static std::optional<long long> read_ll(const std::filesystem::path& p){
    std::ifstream f(p);
    if (!f.good()) return std::nullopt;
    long long v=0;
    f >> v;
    if (!f.fail()) return v;
    return std::nullopt;
}

struct Stats {
    uint64_t events_seen{0};
    uint64_t events_ignored{0};
    uint64_t debounced{0};
    uint64_t staged{0};
    uint64_t commits_ok{0};
    uint64_t commits_fail{0};
    uint64_t git_retries{0};
    uint64_t git_errors{0};
};

struct Pending {
    EventKind kind;
    bool is_dir;
    std::chrono::steady_clock::time_point last;
};

static void setup_logging(const Cli& cli){
    if (cli.log_file) {
        try {
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                cli.log_file->string(), cli.log_rotate_max, cli.log_rotate_files);
            auto logger = std::make_shared<spdlog::logger>("sysaudit", sink);
            spdlog::set_default_logger(logger);
        } catch (...) {
            spdlog::warn("failed to initialize rotating log sink, fallback to default stderr");
        }
    }
    if (cli.verbose) spdlog::set_level(spdlog::level::debug);
}

int main(int argc, char** argv) {
    auto cli = parse_cli(argc, argv);
    if (!cli) return 2;

    setup_logging(*cli);
    spdlog::info("sysaudit starting; watch_dir={}", cli->watch_dir.string());

    if (!std::filesystem::exists(cli->watch_dir) || !std::filesystem::is_directory(cli->watch_dir)) {
        spdlog::error("watch dir doesn't exist or not a directory: {}", cli->watch_dir.string());
        return 1;
    }

    GitRepo repo(cli->watch_dir);
    if (!repo.ensure_initialized()) {
        spdlog::error("Failed to initialize/open git repo at {}", cli->watch_dir.string());
        return 1;
    }
    repo.ensure_default_gitignore();

    if (cli->initial_snapshot) {
        int rc = 0;
        if (repo.add_all_and_commit(fmt::format("sysaudit: initial snapshot ts={}", iso8601_now()), rc)) {
            spdlog::info("initial snapshot committed");
        } else {
            spdlog::warn("initial snapshot failed rc={}", rc);
        }
    }

    PathFilter filter(cli->watch_dir, cli->ignore_exts);
    for (auto& p : cli->excludes) filter.add_pattern(p, false);
    for (auto& p : cli->includes) filter.add_pattern(p, true);
    std::optional<std::filesystem::path> ignore_src = cli->ignore_file;
    if (ignore_src) {
        filter.load_patterns_from_file(*ignore_src);
    } else {
        auto def = cli->watch_dir / ".sysauditignore";
        if (std::filesystem::exists(def)) {
            ignore_src = def;
            filter.load_patterns_from_file(def);
        }
    }

    DirWatcher watcher(cli->watch_dir);

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP,  on_signal);

    Stats stats;
    std::unordered_map<std::string, Pending> pending;
    auto now = std::chrono::steady_clock::now();
    auto last_stats = now;
    auto last_batch = now;

    auto flush_ready = [&](bool force){
        using namespace std::chrono;
        const auto debounce = milliseconds(cli->debounce_ms);
        std::vector<std::pair<std::string, Pending>> ready;
        ready.reserve(pending.size());
        auto tnow = steady_clock::now();
        for (auto it = pending.begin(); it != pending.end();) {
            if (force || tnow - it->second.last >= debounce) {
                ready.emplace_back(*it);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
        if (ready.empty()) return;

        bool batch_mode = cli->batch_count.has_value() || cli->batch_window_ms.has_value();
        if (batch_mode) {
            int rc = 0;
            if (!repo.add_all_and_commit(fmt::format("sysaudit: {} changes ts={}", ready.size(), iso8601_now()), rc)) {
                stats.commits_fail++;
                if (rc != 0) stats.git_errors++;
            } else {
                stats.commits_ok++;
                stats.staged += ready.size();
            }
            last_batch = tnow;
        } else {
            for (auto& kv : ready) {
                std::filesystem::path pth = kv.first;
                auto kind = kv.second.kind;
                bool ok = false;
                std::string msg = fmt::format("op={} path={} ts={}", to_string(kind), pth.string(), iso8601_now());
                switch (kind){
                    case EventKind::Create:
                    case EventKind::Modify:
                    case EventKind::MoveTo:
                        ok = repo.add_and_commit(pth, msg);
                        break;
                    case EventKind::Delete:
                    case EventKind::MoveFrom:
                        ok = repo.remove_and_commit(pth, msg);
                        break;
                }
                if (ok) { stats.commits_ok++; stats.staged++; }
                else { stats.commits_fail++; stats.git_errors++; }
            }
        }
    };

    auto reload_filters = [&](){
        if (!g_reload.exchange(false)) return;
        PathFilter nf(cli->watch_dir, cli->ignore_exts);
        for (auto& p : cli->excludes) nf.add_pattern(p, false);
        for (auto& p : cli->includes) nf.add_pattern(p, true);
        if (ignore_src && std::filesystem::exists(*ignore_src)) {
            nf.load_patterns_from_file(*ignore_src);
        }
        filter = std::move(nf);
        spdlog::info("filters reloaded");
    };

    auto on_event = [&](const FileEvent& ev){
        stats.events_seen++;

        if (filter.is_ignored(ev.path, ev.is_dir)) {
            stats.events_ignored++;
            spdlog::debug("ignored: {}", ev.path.string());
            return;
        }
        if (ev.is_dir && ev.kind != EventKind::Delete && ev.kind != EventKind::MoveFrom) {
            spdlog::debug("skip dir: {}", ev.path.string());
            return;
        }

        using namespace std::chrono;
        auto key = std::filesystem::weakly_canonical(ev.path).string();
        auto tnow = steady_clock::now();
        auto it = pending.find(key);
        if (it == pending.end()) {
            pending.emplace(key, Pending{ev.kind, ev.is_dir, tnow});
        } else {
            it->second.kind = ev.kind;
            if (tnow - it->second.last < milliseconds(cli->debounce_ms)) stats.debounced++;
            it->second.last = tnow;
        }

        bool batch_mode = cli->batch_count.has_value() || cli->batch_window_ms.has_value();
        if (batch_mode) {
            size_t cnt = pending.size();
            bool hit_count = cli->batch_count.has_value() && cnt >= static_cast<size_t>(*cli->batch_count);
            bool hit_time = false;
            if (cli->batch_window_ms.has_value()) {
                hit_time = (tnow - last_batch) >= milliseconds(*cli->batch_window_ms);
            }
            if (hit_count || hit_time) flush_ready(false);
        }
    };

    auto on_tick = [&](){
        auto tnow = std::chrono::steady_clock::now();
        reload_filters();
        flush_ready(false);
        if (cli->stats && (tnow - last_stats) >= std::chrono::seconds(cli->stats_interval_sec)) {
            spdlog::info("stats: seen={} ignored={} debounced={} staged={} commits_ok={} commits_fail={} git_retries={} git_errors={}",
                         stats.events_seen, stats.events_ignored, stats.debounced, stats.staged,
                         stats.commits_ok, stats.commits_fail, stats.git_retries, stats.git_errors);
            last_stats = tnow;
        }
    };

    if (!watcher.open_recursive()) {
        spdlog::error("failed to open recursive inotify");
        return 1;
    }

    auto watches = watcher.watch_count();
    auto maxw = read_ll("/proc/sys/fs/inotify/max_user_watches");
    if (maxw && watches > static_cast<size_t>(*maxw * 8 / 10)) {
        spdlog::warn("inotify watches: {} of limit {} (>=80%)", watches, *maxw);
    }

    spdlog::info("Watching (recursive) {}", cli->watch_dir.string());
    watcher.run_loop(g_stop, on_event, on_tick);

    flush_ready(true);
    spdlog::info("Stopping. Bye.");
    return 0;
}
