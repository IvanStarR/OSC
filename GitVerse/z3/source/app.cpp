#include <gitproc/app.hpp>
#include <gitproc/cli.hpp>
#include <gitproc/git.hpp>
#include <gitproc/io.hpp>
#include <gitproc/process.hpp>
#include <gitproc/state.hpp>
#include <gitproc/supervisor.hpp>
#include <gitproc/unit.hpp>
#include <gitproc/watcher.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifndef GITPROC_COMMIT
#define GITPROC_COMMIT "unknown"
#endif
#ifndef GITPROC_BRANCH
#define GITPROC_BRANCH "unknown"
#endif
#ifndef GITPROC_BUILD_TIME
#define GITPROC_BUILD_TIME "unknown"
#endif

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace gitproc {

static void print_help() {
  std::cout <<
      R"(gitproc - process manager (git + watcher + health)

Usage:
  gitproc start|stop|status <name|unit_path> [--json]
  gitproc start|stop|status --repo <path|url> --unit <rel_or_path> [--json]
  gitproc restart|reload     <name|unit_path>
  gitproc restart|reload     --repo <path|url> --unit <rel_or_path>
  gitproc reopen-logs        <name|unit_path>
  gitproc reopen-logs        --repo <path|url> --unit <rel_or_path>

  gitproc list
  gitproc logs <name> [--follow] [--lines N]

  gitproc run --repo <path|url> --unit <rel_or_path> [--branch main] [--autosync-sec 5]
)";
}

static fs::path default_run_dir() { return fs::path("run"); }
static fs::path default_logs_dir() { return fs::path("logs"); }

static fs::path resolve_target_to_unit(const std::string &target) {
  fs::path t(target);
  if (t.is_absolute() || t.string().find('/') != std::string::npos ||
      t.extension() == ".unit" || t.extension() == ".service") {
    return t;
  }
  fs::path cand1 = fs::path("services") / (target + ".service");
  fs::path cand2 = fs::path("services") / (target + ".unit");
  if (fs::exists(cand1))
    return cand1;
  if (fs::exists(cand2))
    return cand2;
  return cand2;
}

static void tail_file(const fs::path &p, int lines) {
  std::ifstream in(p, std::ios::binary);
  if (!in) {
    std::cout << "(no file: " << p.string() << ")\n";
    return;
  }
  in.seekg(0, std::ios::end);
  auto pos = in.tellg();
  int count = 0;
  std::string buf;
  for (long long i = static_cast<long long>(pos) - 1; i >= 0; --i) {
    in.seekg(i);
    char c;
    in.get(c);
    if (c == '\n') {
      if (++count > lines) {
        in.seekg(i + 1);
        break;
      }
    }
  }
  while (std::getline(in, buf))
    std::cout << buf << "\n";
}

static Unit load_unit_from(const std::optional<RepoUnit> &ru,
                           const std::optional<Target> &tgt) {
  if (ru) {
    GitRepo repo = GitRepo::open(ru->repo);
    fs::path unit = repo.resolve_unit(ru->unit);
    return Unit::Load(unit);
  } else {
    fs::path unit = resolve_target_to_unit(tgt->value);
    return Unit::Load(unit);
  }
}

int App::run(int argc, char **argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  auto pr = parse_cli(argc, argv);
  if (!pr.cmd) {
    if (!pr.error.empty())
      spdlog::error("{}", pr.error);
    print_help();
    return pr.error.empty() ? 0 : 2;
  }

  const fs::path run_dir = default_run_dir();
  const fs::path logs_dir = default_logs_dir();
  fs::create_directories(run_dir);
  fs::create_directories(logs_dir);

  return std::visit(
      [&](auto &&c) -> int {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, CmdHelp>) {
          print_help();
          return 0;

        } else if constexpr (std::is_same_v<T, CmdVersion>) {
          std::cout << fmt::format("gitproc {} ({}, built {})\n",
                                   GITPROC_COMMIT, GITPROC_BRANCH,
                                   GITPROC_BUILD_TIME);
          return 0;

        } else if constexpr (std::is_same_v<T, CmdStart>) {
          Unit u = load_unit_from(c.ru, c.target);
          return ProcessRunner::start(u, run_dir, logs_dir);

        } else if constexpr (std::is_same_v<T, CmdStop>) {
          Unit u = load_unit_from(c.ru, c.target);
          return ProcessRunner::stop(u, run_dir) ? 0 : 1;

        } else if constexpr (std::is_same_v<T, CmdStatus>) {
          Unit u = load_unit_from(c.ru, c.target);
          auto st = ProcessRunner::status(u, run_dir);
          if (c.json.value_or(false)) {
            std::cout << fmt::format(
                             R"({{"running":{},"pid":{},"last_exit_code":{}}})",
                             st.running ? "true" : "false", st.pid,
                             st.last_exit_code)
                      << "\n";
          } else {
            std::cout << (st.running ? "running" : "stopped")
                      << " pid=" << st.pid << " exit=" << st.last_exit_code
                      << "\n";
          }
          return 0;

        } else if constexpr (std::is_same_v<T, CmdRestart>) {
          Unit u = load_unit_from(c.ru, c.target);
          (void)ProcessRunner::stop(u, run_dir);
          return ProcessRunner::start(u, run_dir, logs_dir);

        } else if constexpr (std::is_same_v<T, CmdReload>) {
          Unit u = load_unit_from(c.ru, c.target);
          bool ok = ProcessRunner::reload(u, run_dir, logs_dir);
          return ok ? 0 : 1;

        } else if constexpr (std::is_same_v<T, CmdReopenLogs>) {
          // Грейс-рестарт ради ротации/переоткрытия логов
          Unit u = load_unit_from(c.ru, c.target);
          (void)ProcessRunner::stop(u, run_dir);
          return ProcessRunner::start(u, run_dir, logs_dir);

        } else if constexpr (std::is_same_v<T, CmdList>) {
          fs::path svc = "services";
          if (!fs::exists(svc)) {
            std::cout << "(no services/)\n";
            return 0;
          }
          for (auto &e : fs::directory_iterator(svc)) {
            if (!e.is_regular_file())
              continue;
            auto ext = e.path().extension().string();
            if (ext == ".unit" || ext == ".service") {
              std::cout << e.path().filename().string() << "\n";
            }
          }
          return 0;

        } else if constexpr (std::is_same_v<T, CmdLogs>) {
          fs::path outp = logs_dir / (c.name + ".out");
          fs::path errp = logs_dir / (c.name + ".err");

          auto print_tail = [&](const fs::path &p, int lines) {
            std::cout << "=== " << p.string() << " (last " << lines
                      << " lines)\n";
            tail_file(p, lines);
          };

          print_tail(outp, c.lines);
          print_tail(errp, c.lines);

          if (c.follow) {
            std::error_code ec;
            std::uintmax_t last_out =
                fs::exists(outp, ec) ? fs::file_size(outp, ec) : 0;
            std::uintmax_t last_err =
                fs::exists(errp, ec) ? fs::file_size(errp, ec) : 0;
            while (true) {
              std::this_thread::sleep_for(500ms);
              ec.clear();
              auto cur_out = fs::exists(outp, ec) ? fs::file_size(outp, ec) : 0;
              if (cur_out > last_out) {
                std::ifstream in(outp, std::ios::binary);
                in.seekg(last_out);
                std::string line;
                while (std::getline(in, line))
                  std::cout << line << "\n";
                last_out = cur_out;
              }
              ec.clear();
              auto cur_err = fs::exists(errp, ec) ? fs::file_size(errp, ec) : 0;
              if (cur_err > last_err) {
                std::ifstream in(errp, std::ios::binary);
                in.seekg(last_err);
                std::string line;
                while (std::getline(in, line))
                  std::cout << line << "\n";
                last_err = cur_err;
              }
            }
          }
          return 0;

        } else if constexpr (std::is_same_v<T, CmdRun>) {
          GitRepo repo = GitRepo::open(c.repo);
          Supervisor sup(run_dir, logs_dir);

          auto start_or_log = [&](const fs::path &up) {
            try {
              Unit u = Unit::Load(up);
              if (!sup.start(u)) {
                spdlog::error("[run] failed to start");
              }
            } catch (const std::exception &e) {
              spdlog::error("[run] start error: {}", e.what());
            }
          };

          auto first_unit = repo.resolve_unit(c.unit);
          spdlog::info("[run] repo={} unit={} branch={} period={}ms",
                       repo.root().string(), c.unit, c.branch,
                       c.autosync_sec * 1000);
          start_or_log(first_unit);

          std::atomic_bool stop{false};
          Watcher w(repo, c.unit, c.branch, [&](const fs::path &up) {
            try {
              Unit u = Unit::Load(up);
              (void)sup.reload(u);
            } catch (const std::exception &e) {
              spdlog::error("[run] reload error: {}", e.what());
            }
          });
          w.run_loop(std::chrono::milliseconds(c.autosync_sec * 1000), stop);
          (void)sup.stop();
          return 0;

        } else {
          spdlog::info("Command not implemented yet.");
          return 0;
        }
      },
      *pr.cmd);
}

} // namespace gitproc
