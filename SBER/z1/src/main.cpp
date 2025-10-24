#include <filesystem>
#include <chrono>
#include <spdlog/spdlog.h>
#include "config.hpp"
#include "thread_pool.hpp"
#include "repoquery.hpp"
#include "graph.hpp"
#include "json_writer.hpp"

int main(){
  Config cfg;
  if(const char* e=getenv("REPOIDS")) cfg.repoids=e;
  if(const char* e=getenv("ARCHS"))   cfg.archs=e;
  if(const char* e=getenv("OUTDIR"))  cfg.outdir=e;
  if(const char* e=getenv("CACHEDIR"))cfg.cachedir=e;
  if(const char* e=getenv("THREADS")) cfg.threads=std::max(0, atoi(e));

  spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
  spdlog::info("repoids={} archs={} outdir={} cachedir={} threads={}",
               cfg.repoids.empty()?"<ALL>":cfg.repoids, cfg.archs, cfg.outdir, cfg.cachedir, cfg.threads?cfg.threads:std::thread::hardware_concurrency());

  std::filesystem::create_directories(cfg.outdir);
  std::filesystem::create_directories(cfg.cachedir);

  auto t0 = std::chrono::steady_clock::now();

  // -------- Runtime graph --------
  spdlog::info("Listing binary packages...");
  auto pkgs = list_all_binary_packages(cfg);
  spdlog::info("packages: {}", pkgs.size());

  Graph gr_runtime;
  for(auto& p: pkgs) gr_runtime.add_node(p,"rpm");

  ThreadPool pool(cfg.threads);
  std::atomic<size_t> done_rt{0};
  for(auto& p: pkgs){
    pool.submit([&cfg,&gr_runtime,&done_rt,p]{
      auto deps = resolve_runtime_requires(cfg,p);
      for(auto& d: deps){
        gr_runtime.add_node(d,"rpm");
        gr_runtime.add_edge(p,d);
      }
      size_t d = ++done_rt;
      if((d%200)==0) spdlog::info("runtime progress: {}/{}", d, (size_t)pkgs.size());
    });
  }
  pool.wait_empty(); // дождаться очереди
  // (pool разрушится в деструкторе после выхода main — ок)

  auto t1 = std::chrono::steady_clock::now();
  spdlog::info("Runtime edges: {}", gr_runtime.edges.size());
  if(!write_graph_json(gr_runtime,"Runtime dependencies", cfg.outdir + "/runtime_graph.json"))
    spdlog::error("cannot write runtime_graph.json");

  // -------- Build graph --------
  spdlog::info("Listing SRPMs...");
  auto srpms = list_all_srpms(cfg);
  spdlog::info("srpms: {}", srpms.size());

  Graph gr_build;
  for(auto& s: srpms) gr_build.add_node(s,"srpm");

  ThreadPool pool2(cfg.threads);
  std::atomic<size_t> done_bd{0};
  for(auto& s: srpms){
    pool2.submit([&cfg,&gr_build,&done_bd,s]{
      auto deps = resolve_build_requires(cfg,s);
      for(auto& d: deps){
        gr_build.add_node(d,"rpm");
        gr_build.add_edge(s,d);
      }
      size_t d = ++done_bd;
      if((d%100)==0) spdlog::info("build progress: {}/{}", d, (size_t)srpms.size());
    });
  }
  pool2.wait_empty();

  auto t2 = std::chrono::steady_clock::now();
  spdlog::info("Build edges: {}", gr_build.edges.size());
  if(!write_graph_json(gr_build,"Build dependencies", cfg.outdir + "/build_graph.json"))
    spdlog::error("cannot write build_graph.json");

  using ns = std::chrono::duration<double>;
  spdlog::info("Timing: runtime {:.2f}s, build {:.2f}s, total {:.2f}s",
    ns(t1-t0).count(), ns(t2-t1).count(), ns(t2-t0).count());
  spdlog::info("DONE");
}
