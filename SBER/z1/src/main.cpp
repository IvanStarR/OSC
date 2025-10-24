#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <thread>
#include <atomic>

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "thread_pool.hpp"
#include "repoquery.hpp"
#include "graph.hpp"
#include "json_writer.hpp"

int main() {
  // --- Конфиг из env ---
  Config cfg;
  if (const char* e = std::getenv("REPOIDS"))  cfg.repoids  = e;
  if (const char* e = std::getenv("ARCHS"))    cfg.archs    = e;
  if (const char* e = std::getenv("OUTDIR"))   cfg.outdir   = e;
  if (const char* e = std::getenv("CACHEDIR")) cfg.cachedir = e;
  if (const char* e = std::getenv("THREADS"))  cfg.threads  = std::max(0, std::atoi(e));

  // --- Логи ---
  spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
  spdlog::info("repoids={} archs={} outdir={} cachedir={} threads={}",
               cfg.repoids.empty() ? "<ALL>" : cfg.repoids,
               cfg.archs,
               cfg.outdir,
               cfg.cachedir,
               cfg.threads ? cfg.threads : (int)std::thread::hardware_concurrency());

  // --- Директории вывода/кеша ---
  try {
    std::filesystem::create_directories(cfg.outdir);
    std::filesystem::create_directories(cfg.cachedir);
  } catch (const std::exception& ex) {
    spdlog::error("cannot create directories: {} (err: {})", cfg.outdir, ex.what());
    return 1;
  }

  auto t_start = std::chrono::steady_clock::now();

  // ============================================================
  //                R U N T I M E   G R A P H
  // ============================================================
  spdlog::info("Listing binary packages...");
  auto pkgs = list_all_binary_packages(cfg);
  spdlog::info("packages: {}", pkgs.size());
  if (pkgs.empty()) {
    spdlog::warn("No binary packages found. Check 'dnf repolist' and run 'sudo dnf makecache'.");
  }

  Graph gr_runtime;
  for (const auto& p : pkgs) gr_runtime.add_node(p, "rpm");

  ThreadPool pool_runtime(cfg.threads);
  std::atomic<size_t> done_rt{0};
  const size_t total_pkgs = pkgs.size(); // захватим в лямбду по значению

  for (const auto& p : pkgs) {
    pool_runtime.submit([&cfg, &gr_runtime, &done_rt, p, total_pkgs] {
      auto deps = resolve_runtime_requires(cfg, p);
      for (const auto& d : deps) {
        gr_runtime.add_node(d, "rpm");
        gr_runtime.add_edge(p, d);
      }
      size_t d = ++done_rt;
      if ((d % 200) == 0) spdlog::info("runtime progress: {}/{}", d, total_pkgs);
    });
  }
  pool_runtime.wait_empty(); // дождаться обработки очереди

  auto t_runtime = std::chrono::steady_clock::now();
  spdlog::info("Runtime edges: {}", gr_runtime.edges.size());
  if (!write_graph_json(gr_runtime, "Runtime dependencies",
                        cfg.outdir + "/runtime_graph.json")) {
    spdlog::error("cannot write runtime_graph.json");
  }

  // ============================================================
  //                  B U I L D   G R A P H
  // ============================================================
  spdlog::info("Listing SRPMs...");
  auto srpms = list_all_srpms(cfg);
  spdlog::info("srpms: {}", srpms.size());
  if (srpms.empty()) {
    spdlog::warn("No SRPMs found. Ensure repos are enabled. Try 'sudo dnf makecache'.");
  }

  Graph gr_build;
  for (const auto& s : srpms) gr_build.add_node(s, "srpm");

  ThreadPool pool_build(cfg.threads);
  std::atomic<size_t> done_bd{0};
  const size_t total_srpms = srpms.size(); // захватим в лямбду по значению

  for (const auto& s : srpms) {
    pool_build.submit([&cfg, &gr_build, &done_bd, s, total_srpms] {
      auto deps = resolve_build_requires(cfg, s);
      for (const auto& d : deps) {
        gr_build.add_node(d, "rpm");
        gr_build.add_edge(s, d);
      }
      size_t d = ++done_bd;
      if ((d % 100) == 0) spdlog::info("build progress: {}/{}", d, total_srpms);
    });
  }
  pool_build.wait_empty();

  auto t_build = std::chrono::steady_clock::now();
  spdlog::info("Build edges: {}", gr_build.edges.size());
  if (!write_graph_json(gr_build, "Build dependencies",
                        cfg.outdir + "/build_graph.json")) {
    spdlog::error("cannot write build_graph.json");
  }

  // --- Тайминги ---
  using seconds_f = std::chrono::duration<double>;
  double dt_runtime = seconds_f(t_runtime - t_start).count();
  double dt_build   = seconds_f(t_build   - t_runtime).count();
  double dt_total   = seconds_f(t_build   - t_start).count();

  spdlog::info("Timing: runtime {:.2f}s, build {:.2f}s, total {:.2f}s",
               dt_runtime, dt_build, dt_total);
  spdlog::info("DONE");
  return 0;
}
