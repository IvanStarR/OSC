#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <thread>
#include <spdlog/spdlog.h>

#include "config.hpp"
#include "graph.hpp"
#include "json_writer.hpp"
#include "solv_backend.hpp"

int main(){
  Config cfg;
  if(const char* e=getenv("REPOIDS"))  cfg.repoids=e;
  if(const char* e=getenv("ARCHS"))    cfg.archs=e;
  if(const char* e=getenv("OUTDIR"))   cfg.outdir=e;
  if(const char* e=getenv("THREADS"))  cfg.threads=std::max(0, atoi(e));

  spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
  spdlog::info("libsolv mode; repoids={} archs={} outdir={} threads={}",
               cfg.repoids.empty()?"<ALL>":cfg.repoids, cfg.archs, cfg.outdir,
               cfg.threads?cfg.threads:(int)std::thread::hardware_concurrency());

  std::filesystem::create_directories(cfg.outdir);

  auto t0 = std::chrono::steady_clock::now();

  Graph gr_runtime, gr_build;
  SolvStats st;
  if(!build_graphs_with_libsolv(cfg.repoids, cfg.archs, gr_runtime, gr_build, st, cfg.threads)){
    spdlog::error("libsolv backend failed");
    return 1;
  }

  auto t1 = std::chrono::steady_clock::now();

  if(!write_graph_json(gr_runtime,"Runtime dependencies", cfg.outdir + "/runtime_graph.json"))
    spdlog::error("cannot write runtime_graph.json");
  if(!write_graph_json(gr_build,  "Build dependencies",   cfg.outdir + "/build_graph.json"))
    spdlog::error("cannot write build_graph.json");

  auto t2 = std::chrono::steady_clock::now();
  using sec = std::chrono::duration<double>;
  spdlog::info("repos={} solvables={} runtime_edges={} build_edges={}",
               st.repos_loaded, st.solvables_seen, st.runtime_edges, st.build_edges);
  spdlog::info("Timing: build_graphs {:.2f}s, write_json {:.2f}s, total {:.2f}s",
               sec(t1-t0).count(), sec(t2-t1).count(), sec(t2-t0).count());
  spdlog::info("DONE");
  return 0;
}
