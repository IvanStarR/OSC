#include "kv.hpp"

#include <spdlog/spdlog.h>
#include <fmt/core.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ----------------------------
// Парсер аргументов / режимы
// ----------------------------
struct Args {
  std::string mode = "run";          // run | bench | put | get | del | scan | metrics
  std::string path = "/tmp/uringkv_demo";

  // опции io/durability/compaction
  bool        use_uring = false;
  unsigned    uring_qd  = 256;
  std::string flush_mode = "fdatasync";          // fdatasync|fsync|sfr
  std::string compaction_policy = "size-tiered"; // size-tiered|leveled
  bool        uring_sqpoll = false;

  // SST/WAL
  uint64_t    wal_segment_bytes   = 64ull * 1024 * 1024;
  uint64_t    wal_group_commit    = (1ull<<20); // ~1MiB полезных данных до fsync
  uint64_t    sst_flush_threshold = 4ull * 1024 * 1024;
  bool        bg_compaction       = true;
  size_t      l0_compact_threshold= 6;
  size_t      table_cache_capacity= 64;

  // bench
  uint64_t ops = 100'000;
  std::string ratio = "90:5:5";
  size_t key_len = 16;
  size_t val_len = 100;
  unsigned threads = 1;

  // kv ops
  std::string key;
  std::string value;
  std::string start;
  std::string end;

  // metrics
  bool watch = false;
  double watch_interval_sec = 1.0;

  bool help = false;
};

static void print_usage(const char* prog) {
  fmt::print(
R"(Usage:
  {0} [options] <run|bench|put|get|del|scan|metrics> [args...]

Common options:
  --path DIR                       : data path (default: /tmp/uringkv_demo)
  --use-uring on|off               : enable io_uring (default: off)
  --queue-depth N                  : io_uring QD (default: 256)
  --uring-sqpoll on|off            : io_uring SQPOLL (default: off)
  --flush fdatasync|fsync|sfr      : durability mode (default: fdatasync)
  --compaction-policy size-tiered|leveled (default: size-tiered)
  --segment BYTES                  : WAL max segment size (default: 64MiB)
  --group-commit BYTES             : WAL group-commit threshold (default: 1MiB)
  --flush-threshold BYTES          : SST flush threshold (default: 4MiB)
  --bg-compact on|off              : background compaction (default: on)
  --l0-threshold N                 : L0 compaction start threshold (default: 6)
  --table-cache N                  : table cache capacity (default: 64)

KV commands:
  put  --key K --value V
  get  --key K
  del  --key K
  scan --start A --end B

Bench options:
  bench                            : run micro-benchmark
  --ops N                          : total operations (default: 100000)
  --ratio PUT:GET:DEL              : mix in percent (default: 90:5:5)
  --key-len N                      : key length bytes (default: 16)
  --val-len N                      : value length bytes (default: 100)
  --threads N                      : worker threads (default: 1)

Metrics:
  metrics                          : print one-time snapshot
  metrics --watch [seconds]        : print periodically (default 1s)

Examples:
  {0} --path /tmp/kv run
  {0} --path /tmp/kv put --key foo --value bar
  {0} --path /tmp/kv scan --start a --end z
  {0} --path /tmp/kv metrics --watch 2
)",
    prog);
}

static bool parse_bool(std::string_view s, bool& out) {
  if (s == "on" || s == "true" || s == "1") { out = true; return true; }
  if (s == "off"|| s == "false"|| s == "0") { out = false; return true; }
  return false;
}

static uint64_t parse_bytes(std::string_view s) {
  if (s.empty()) return 0;
  char unit = s.back();
  uint64_t mul = 1;
  std::string_view num = s;
  if (unit=='K'||unit=='k'||unit=='M'||unit=='m'||unit=='G'||unit=='g') {
    num.remove_suffix(1);
    if (unit=='K'||unit=='k') mul = 1024ull;
    if (unit=='M'||unit=='m') mul = 1024ull*1024ull;
    if (unit=='G'||unit=='g') mul = 1024ull*1024ull*1024ull;
  }
  return std::strtoull(std::string(num).c_str(), nullptr, 10) * mul;
}

static Args parse_args(int argc, char** argv) {
  Args a;
  for (int i=1;i<argc;++i) {
    std::string_view t = argv[i];
    if (t=="-h" || t=="--help") { a.help=true; break; }

    auto need_value = [&](int i)->bool { return (i+1)<argc; };

    if (t=="run"||t=="bench"||t=="put"||t=="get"||t=="del"||t=="scan"||t=="metrics") { a.mode = std::string(t); continue; }
    if (t=="--path" && need_value(i)) { a.path = argv[++i]; continue; }
    if (t=="--use-uring" && need_value(i)) { if(!parse_bool(argv[++i], a.use_uring)) a.help=true; continue; }
    if (t=="--queue-depth" && need_value(i)) { a.uring_qd = std::strtoul(argv[++i],nullptr,10); continue; }
    if (t=="--uring-sqpoll" && need_value(i)) { if(!parse_bool(argv[++i], a.uring_sqpoll)) a.help=true; continue; }
    if (t=="--flush" && need_value(i)) { a.flush_mode = argv[++i]; continue; }
    if (t=="--compaction-policy" && need_value(i)) { a.compaction_policy = argv[++i]; continue; }
    if (t=="--segment" && need_value(i)) { a.wal_segment_bytes = parse_bytes(argv[++i]); continue; }
    if (t=="--group-commit" && need_value(i)) { a.wal_group_commit = parse_bytes(argv[++i]); continue; }
    if (t=="--flush-threshold" && need_value(i)) { a.sst_flush_threshold = parse_bytes(argv[++i]); continue; }
    if (t=="--bg-compact" && need_value(i)) { if(!parse_bool(argv[++i], a.bg_compaction)) a.help=true; continue; }
    if (t=="--l0-threshold" && need_value(i)) { a.l0_compact_threshold = std::strtoul(argv[++i],nullptr,10); continue; }
    if (t=="--table-cache" && need_value(i)) { a.table_cache_capacity = std::strtoul(argv[++i],nullptr,10); continue; }

    if (t=="--ops" && need_value(i)) { a.ops = std::strtoull(argv[++i],nullptr,10); continue; }
    if (t=="--ratio" && need_value(i)) { a.ratio = argv[++i]; continue; }
    if (t=="--key-len" && need_value(i)) { a.key_len = std::strtoul(argv[++i],nullptr,10); continue; }
    if (t=="--val-len" && need_value(i)) { a.val_len = std::strtoul(argv[++i],nullptr,10); continue; }
    if (t=="--threads" && need_value(i)) { a.threads = std::strtoul(argv[++i],nullptr,10); continue; }

    if (t=="--key" && need_value(i)) { a.key = argv[++i]; continue; }
    if (t=="--value" && need_value(i)) { a.value = argv[++i]; continue; }
    if (t=="--start" && need_value(i)) { a.start = argv[++i]; continue; }
    if (t=="--end" && need_value(i)) { a.end = argv[++i]; continue; }

    if (t=="--watch") {
      a.watch = true;
      if (need_value(i) && argv[i+1][0] != '-') {
        a.watch_interval_sec = std::strtod(argv[++i], nullptr);
        if (a.watch_interval_sec <= 0.0) a.watch_interval_sec = 1.0;
      }
      continue;
    }

    spdlog::warn("Unknown arg: {}", t);
    a.help = true;
  }
  return a;
}

// ----------------------------
// Вспомогалки
// ----------------------------
template<class T>
static T percentile(std::vector<T>& v, double p) {
  if (v.empty()) return T{};
  size_t idx = static_cast<size_t>(std::clamp(p, 0.0, 100.0) / 100.0 * (v.size()-1));
  std::nth_element(v.begin(), v.begin()+idx, v.end());
  return v[idx];
}

static std::string rand_key(std::mt19937_64& rng, size_t len) {
  static const char alphabet[] =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::uniform_int_distribution<size_t> dist(0, sizeof(alphabet)-2);
  std::string s; s.resize(len);
  for (size_t i=0;i<len;++i) s[i]=alphabet[dist(rng)];
  return s;
}
static std::string rand_value(std::mt19937_64& rng, size_t len) {
  std::string s; s.resize(len, '\0');
  std::uniform_int_distribution<int> dist(0, 255);
  for (size_t i=0;i<len;++i) s[i]=static_cast<char>(dist(rng));
  return s;
}

// ----------------------------
// bench worker
// ----------------------------
struct BenchStats {
  uint64_t put_cnt=0, get_cnt=0, del_cnt=0;
  std::vector<double> put_lat, get_lat, del_lat; // мкс
};

static void bench_worker(unsigned tid, const Args& a, uringkv::KVOptions base_opts,
                         uint64_t ops, uint32_t pct_put, uint32_t pct_get, [[maybe_unused]]uint32_t pct_del,
                         BenchStats& out)
{
  uringkv::KV kv(base_opts);

  std::mt19937_64 rng(0xBADC0FFEEULL + tid);
  std::uniform_int_distribution<uint32_t> dice(1,100);

  out.put_lat.reserve(ops);
  out.get_lat.reserve(ops);
  out.del_lat.reserve(ops);

  std::vector<std::string> keys;
  keys.reserve(ops/2);

  auto now = []{ return std::chrono::steady_clock::now(); };

  for (uint64_t i=0;i<ops;++i) {
    uint32_t r = dice(rng);
    if (r <= pct_put) {
      std::string k = rand_key(rng, a.key_len);
      std::string v = rand_value(rng, a.val_len);
      auto t0 = now();
      kv.put(k, v);
      auto t1 = now();
      out.put_lat.push_back(std::chrono::duration<double,std::micro>(t1-t0).count());
      ++out.put_cnt;
      if (keys.size()<100000) keys.push_back(std::move(k));
    } else if (r <= pct_put + pct_get) {
      if (keys.empty()) continue;
      const std::string& k = keys[rng()%keys.size()];
      auto t0 = now();
      (void)kv.get(k);
      auto t1 = now();
      out.get_lat.push_back(std::chrono::duration<double,std::micro>(t1-t0).count());
      ++out.get_cnt;
    } else {
      if (keys.empty()) continue;
      const std::string& k = keys[rng()%keys.size()];
      auto t0 = now();
      kv.del(k);
      auto t1 = now();
      out.del_lat.push_back(std::chrono::duration<double,std::micro>(t1-t0).count());
      ++out.del_cnt;
    }
  }
}

// ----------------------------
// helpers for metrics printing
// ----------------------------
static void print_metrics_once(const uringkv::KVMetrics& m) {
  auto hit_total = m.get_hits + m.get_misses;
  double hit_rate = hit_total ? (100.0 * double(m.get_hits) / double(hit_total)) : 0.0;

  fmt::print("=== uringkv metrics ===\n");
  fmt::print("ops:   puts={} gets={} dels={}\n", m.puts, m.gets, m.dels);
  fmt::print("gets:  hits={} misses={} hit_rate={:.2f}%\n", m.get_hits, m.get_misses, hit_rate);
  fmt::print("wal:   bytes_written={}\n", m.wal_bytes);
  fmt::print("sst:   flushes={} compactions={} sst_count={}\n", m.sst_flushes, m.compactions, m.sst_count);
  fmt::print("mem:   mem_bytes={}\n", m.mem_bytes);
  fmt::print("tcache:hits={} misses={} opens={}\n", m.table_cache_hits, m.table_cache_misses, m.table_cache_opens);
}

static void print_metrics_diff(const uringkv::KVMetrics& prev, const uringkv::KVMetrics& cur, double dt_sec) {
  auto d = [&](uint64_t a, uint64_t b){ return (b>=a) ? (b-a) : 0ull; };
  uint64_t d_put  = d(prev.puts, cur.puts);
  uint64_t d_get  = d(prev.gets, cur.gets);
  uint64_t d_del  = d(prev.dels, cur.dels);
  uint64_t d_hit  = d(prev.get_hits, cur.get_hits);
  uint64_t d_miss = d(prev.get_misses, cur.get_misses);
  uint64_t d_wal  = d(prev.wal_bytes, cur.wal_bytes);
  uint64_t d_fl   = d(prev.sst_flushes, cur.sst_flushes);
  uint64_t d_cmp  = d(prev.compactions, cur.compactions);

  double r_put = d_put / dt_sec;
  double r_get = d_get / dt_sec;
  double r_del = d_del / dt_sec;
  double r_wal = d_wal / dt_sec;

  auto hit_total = d_hit + d_miss;
  double hit_rate = hit_total ? (100.0 * double(d_hit) / double(hit_total)) : 0.0;

  fmt::print("Δ{:.2f}s: put={:.0f}/s get={:.0f}/s del={:.0f}/s  hit={}% miss={} (hit={:.2f}%)  wal={:.0f} B/s  +flushes={} +compactions={}\n",
             dt_sec, r_put, r_get, r_del,
             d_hit, d_miss, hit_rate,
             r_wal, d_fl, d_cmp);
}

// ----------------------------
// main
// ----------------------------
int main(int argc, char** argv) {
  auto a = parse_args(argc, argv);
  if (a.help) { print_usage(argv[0]); return 0; }

  // ratio
  uint32_t putP=90,getP=5,delP=5;
  {
    auto pos1 = a.ratio.find(':');
    auto pos2 = a.ratio.rfind(':');
    if (pos1!=std::string::npos && pos2!=std::string::npos && pos1!=pos2) {
      putP = std::strtoul(a.ratio.substr(0,pos1).c_str(),nullptr,10);
      getP = std::strtoul(a.ratio.substr(pos1+1,pos2-pos1-1).c_str(),nullptr,10);
      delP = std::strtoul(a.ratio.substr(pos2+1).c_str(),nullptr,10);
      if (putP+getP+delP==0) { putP=90; getP=5; delP=5; }
    }
  }

  // KVOptions
  uringkv::KVOptions opts;
  opts.path                        = a.path;
  opts.use_uring                  = a.use_uring;
  opts.uring_queue_depth          = a.uring_qd;
  opts.uring_sqpoll               = a.uring_sqpoll;
  opts.wal_max_segment_bytes      = a.wal_segment_bytes;
  opts.wal_group_commit_bytes     = a.wal_group_commit;
  opts.sst_flush_threshold_bytes  = a.sst_flush_threshold;
  opts.background_compaction      = a.bg_compaction;
  opts.l0_compact_threshold       = a.l0_compact_threshold;
  opts.table_cache_capacity       = a.table_cache_capacity;

  // flush mode
  if (a.flush_mode == "fdatasync") opts.flush_mode = uringkv::FlushMode::FDATASYNC;
  else if (a.flush_mode == "fsync") opts.flush_mode = uringkv::FlushMode::FSYNC;
  else if (a.flush_mode == "sfr")   opts.flush_mode = uringkv::FlushMode::SYNC_FILE_RANGE;
  else { spdlog::error("Unknown --flush '{}'", a.flush_mode); return 2; }

  // compaction policy (leveled заглушка)
  if (a.compaction_policy == "size-tiered") opts.compaction_policy = uringkv::CompactionPolicy::SIZE_TIERED;
  else if (a.compaction_policy == "leveled") opts.compaction_policy = uringkv::CompactionPolicy::LEVELED;
  else { spdlog::error("Unknown --compaction-policy '{}'", a.compaction_policy); return 2; }

  if (a.mode == "run") {
    uringkv::KV kv(opts);
    if (!kv.init_storage_layout()) {
      spdlog::error("Failed to init storage layout at {}", a.path);
      return 1;
    }
    spdlog::info("uringkv is ready at {}", a.path);
    return 0;
  }

  if (a.mode == "put") {
    if (a.key.empty()) { spdlog::error("put: --key required"); return 2; }
    uringkv::KV kv(opts);
    if (!kv.init_storage_layout()) { spdlog::error("init failed"); return 1; }
    bool ok = kv.put(a.key, a.value);
    fmt::print("{}\n", ok ? "OK" : "ERR");
    return ok ? 0 : 1;
  }

  if (a.mode == "get") {
    if (a.key.empty()) { spdlog::error("get: --key required"); return 2; }
    uringkv::KV kv(opts);
    if (!kv.init_storage_layout()) { spdlog::error("init failed"); return 1; }
    auto v = kv.get(a.key);
    if (!v) { fmt::print("(nil)\n"); return 1; }
    fmt::print("{}\n", *v);
    return 0;
  }

  if (a.mode == "del") {
    if (a.key.empty()) { spdlog::error("del: --key required"); return 2; }
    uringkv::KV kv(opts);
    if (!kv.init_storage_layout()) { spdlog::error("init failed"); return 1; }
    bool ok = kv.del(a.key);
    fmt::print("{}\n", ok ? "OK" : "ERR");
    return ok ? 0 : 1;
  }

  if (a.mode == "scan") {
    uringkv::KV kv(opts);
    if (!kv.init_storage_layout()) { spdlog::error("init failed"); return 1; }
    auto items = kv.scan(a.start, a.end);
    for (auto& it : items) {
      fmt::print("{} {}\n", it.key, it.value);
    }
    return 0;
  }

  if (a.mode == "bench") {
    // init layout once
    {
      uringkv::KV kv(opts);
      if (!kv.init_storage_layout()) {
        spdlog::error("Failed to init storage layout at {}", a.path);
        return 1;
      }
    }

    unsigned th = std::max(1u, a.threads);
    uint64_t per = a.ops / th;
    uint64_t rem = a.ops % th;

    std::vector<std::thread> workers;
    std::vector<BenchStats> stats(th);
    auto t0 = std::chrono::steady_clock::now();

    for (unsigned i=0;i<th;++i) {
      uint64_t my_ops = per + (i < rem ? 1 : 0);
      workers.emplace_back(bench_worker, i, a, opts, my_ops, putP, getP, delP, std::ref(stats[i]));
    }
    for (auto& t : workers) t.join();

    auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1-t0).count();

    BenchStats tot;
    for (auto& s : stats) {
      tot.put_cnt += s.put_cnt; tot.get_cnt += s.get_cnt; tot.del_cnt += s.del_cnt;
      tot.put_lat.insert(tot.put_lat.end(), s.put_lat.begin(), s.put_lat.end());
      tot.get_lat.insert(tot.get_lat.end(), s.get_lat.begin(), s.get_lat.end());
      tot.del_lat.insert(tot.del_lat.end(), s.del_lat.begin(), s.del_lat.end());
    }

    auto print_class = [&](std::string_view name, uint64_t cnt, std::vector<double>& lat){
      double tps = cnt / sec;
      double p50 = percentile(lat, 50.0);
      double p95 = percentile(lat, 95.0);
      double p99 = percentile(lat, 99.0);
      fmt::print("{}: ops={} ({} ops/s)  latency_us: p50={:.2f} p95={:.2f} p99={:.2f}\n",
                 name, cnt, static_cast<uint64_t>(tps), p50, p95, p99);
    };

    fmt::print("=== uringkv bench @ {} (threads={}, ratio={} PUT:GET:DEL) ===\n",
               a.path, th, a.ratio);
    fmt::print("opts: uring={} qd={} sqpoll={} segment={}B group-commit={}B flush={} bg_compact={} l0_thr={} table_cache={} policy={}\n",
               (a.use_uring?"on":"off"), a.uring_qd, (a.uring_sqpoll?"on":"off"),
               a.wal_segment_bytes, a.wal_group_commit, a.flush_mode,
               (a.bg_compaction?"on":"off"), a.l0_compact_threshold, a.table_cache_capacity, a.compaction_policy);
    fmt::print("total ops: {}  elapsed: {:.3f} s  overall: {} ops/s\n\n",
               a.ops, sec, static_cast<uint64_t>(a.ops/sec));

    print_class("PUT", tot.put_cnt, tot.put_lat);
    print_class("GET", tot.get_cnt, tot.get_lat);
    print_class("DEL", tot.del_cnt, tot.del_lat);
    return 0;
  }

  if (a.mode == "metrics") {
    uringkv::KV kv(opts);
    if (!kv.init_storage_layout()) {
      spdlog::error("Failed to init storage layout at {}", a.path);
      return 1;
    }

    auto snap = kv.get_metrics();
    print_metrics_once(snap);
    if (!a.watch) return 0;

    auto prev = snap;
    auto t_prev = std::chrono::steady_clock::now();

    while (true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(
        static_cast<int>(a.watch_interval_sec * 1000)));

      auto t_now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(t_now - t_prev).count();

      auto cur = kv.get_metrics();
      print_metrics_diff(prev, cur, dt);
      prev = cur;
      t_prev = t_now;
    }
  }

  spdlog::error("Unknown mode: {}", a.mode);
  print_usage(argv[0]);
  return 2;
}
