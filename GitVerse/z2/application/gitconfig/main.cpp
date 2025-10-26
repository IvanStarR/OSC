#include "gitconfig/kv.hpp"
#include "gitconfig/history.hpp"
#include "gitconfig/conflict.hpp"
#include "gitconfig/nodes.hpp"
#include "application/gitconfigd/server.hpp"
#include "application/gitconfigd/router.hpp"
#include "application/gitconfigd/sync_loop.hpp"
#include <fmt/format.h>
#include <string>
#include <vector>
#include <optional>
#include <iostream>
#include <cstdlib>
#include <algorithm>

namespace gitconfig_cli {

struct Args {
  std::string cmd;
  std::vector<std::string> pos;
  std::string repo;
  std::string data_root = "config";
  bool recursive = false;
  std::string remote = "origin";
  std::string branch = "main";
  std::string url;
  std::string commit;
  int limit = 20;
  std::optional<std::string> defval;
  std::string addr = "0.0.0.0";
  unsigned short port = 8080;
  int sync_interval = 0;
  bool json = false;
};

static Args parse(int argc, char** argv) {
  Args a{};
  if (argc>1) a.cmd = argv[1];
  for (int i=2;i<argc;++i) {
    std::string s = argv[i];
    if (s=="--repo" && i+1<argc) a.repo = argv[++i];
    else if (s=="--data-root" && i+1<argc) a.data_root = argv[++i];
    else if (s=="--recursive") a.recursive = true;
    else if (s=="--remote" && i+1<argc) a.remote = argv[++i];
    else if (s=="--branch" && i+1<argc) a.branch = argv[++i];
    else if (s=="--url" && i+1<argc) a.url = argv[++i];
    else if (s=="--commit" && i+1<argc) a.commit = argv[++i];
    else if (s=="--limit" && i+1<argc) a.limit = std::atoi(argv[++i]);
    else if (s=="--default" && i+1<argc) a.defval = std::string(argv[++i]);
    else if (s=="--addr" && i+1<argc) a.addr = argv[++i];
    else if (s=="--port" && i+1<argc) a.port = static_cast<unsigned short>(std::stoi(argv[++i]));
    else if (s=="--sync-interval" && i+1<argc) a.sync_interval = std::atoi(argv[++i]);
    else if (s=="--json") a.json = true;
    else a.pos.push_back(s);
  }
  if (a.repo.empty()) a.repo = ".";
  return a;
}

static int cmd_init(const Args& a) {
  gitconfig::KVStore kv({a.repo, a.data_root});
  std::string err;
  if (!kv.init(&err)) {
    std::cerr << fmt::format("init failed: {}\n", err);
    return 1;
  }
  std::cout << fmt::format("initialized {}\n", a.repo);
  return 0;
}

static int cmd_set(const Args& a) {
  if (a.pos.size()<2) { std::cerr << "usage: set <key> <value> [--repo PATH]\n"; return 2; }
  gitconfig::KVStore kv({a.repo, a.data_root});
  std::string err;
  if (!kv.set(a.pos[0], a.pos[1], &err)) {
    std::cerr << fmt::format("set failed: {}\n", err);
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}

static int cmd_get(const Args& a) {
  if (a.pos.size()<1) { std::cerr << "usage: get <key> [--commit SHA] [--default VAL] [--repo PATH]\n"; return 2; }
  gitconfig::KVStore kv({a.repo, a.data_root});
  std::string err;
  if (!a.commit.empty()) {
    auto v = kv.get_at(a.pos[0], a.commit, &err);
    if (!v.has_value()) {
      if (a.defval.has_value()) { std::cout << *a.defval << "\n"; return 0; }
      std::cerr << "key not found\n"; return 3;
    }
    std::cout << *v << "\n";
    return 0;
  } else {
    auto v = kv.get(a.pos[0], &err);
    if (!v.has_value()) {
      if (a.defval.has_value()) { std::cout << *a.defval << "\n"; return 0; }
      std::cerr << "key not found\n"; return 3;
    }
    std::cout << *v << "\n";
    return 0;
  }
}

static int cmd_exists(const Args& a) {
  if (a.pos.size()<1) { std::cerr << "usage: exists <key> --repo <path>\n"; return 2; }
  gitconfig::KVStore kv({a.repo, a.data_root});
  bool ex = kv.exists(a.pos[0]);
  std::cout << (ex ? "1\n" : "0\n");
  return ex ? 0 : 4;
}

static int cmd_delete(const Args& a) {
  if (a.pos.size()<1) { std::cerr << "usage: delete <key> [--repo PATH]\n"; return 2; }
  gitconfig::KVStore kv({a.repo, a.data_root});
  std::string err;
  if (!kv.erase(a.pos[0], &err)) {
    std::cerr << fmt::format("delete failed: {}\n", err);
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}

static int cmd_list(const Args& a) {
  std::string prefix = "/";
  if (!a.pos.empty()) prefix = a.pos[0];
  gitconfig::KVStore kv({a.repo, a.data_root});
  std::string err;
  auto entries = kv.list(prefix, a.recursive, &err);
  std::sort(entries.begin(), entries.end(), [](const gitconfig::ListEntry& x, const gitconfig::ListEntry& y){
    if (x.key==y.key) return x.is_dir && !y.is_dir;
    return x.key < y.key;
  });
  if (a.json) {
    std::cout << "[";
    for (size_t i=0;i<entries.size();++i) {
      const auto& e = entries[i];
      std::cout << fmt::format("{{\"key\":\"{}\",\"dir\":{}}}", e.key, e.is_dir? "true":"false");
      if (i+1<entries.size()) std::cout << ",";
    }
    std::cout << "]\n";
  } else {
    for (auto& e : entries) {
      std::cout << fmt::format("{}{}\n", e.key, e.is_dir?"/":"");
    }
  }
  return 0;
}

static int cmd_remote_set(const Args& a) {
  if (a.url.empty()) {
    std::cerr << "usage: remote set --repo <path> --remote <name> --url <url>\n";
    return 2;
  }
  gitconfig::Repo r(a.repo);
  std::string err;
  if (!r.ensure_initialized("gitconfig", "gitconfig@localhost", &err)) {
    std::cerr << fmt::format("repo init failed: {}\n", err);
    return 1;
  }
  if (!r.set_remote(a.remote, a.url, &err)) {
    std::cerr << fmt::format("remote set failed: {}\n", err);
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}

static int cmd_push(const Args& a) {
  gitconfig::Repo r(a.repo);
  std::string err;
  if (!r.ensure_initialized("gitconfig", "gitconfig@localhost", &err)) {
    std::cerr << fmt::format("repo init failed: {}\n", err);
    return 1;
  }
  if (!r.push(a.remote, a.branch, &err)) {
    std::cerr << fmt::format("push failed: {}\n", err);
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}

static int cmd_pull(const Args& a) {
  gitconfig::Repo r(a.repo);
  std::string err;
  if (!r.ensure_initialized("gitconfig", "gitconfig@localhost", &err)) {
    std::cerr << fmt::format("repo init failed: {}\n", err);
    return 1;
  }
  if (!r.pull(a.remote, a.branch, &err)) {
    std::cerr << fmt::format("pull failed: {}\n", err);
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}

static int cmd_history(const Args& a) {
  if (a.pos.size()<1) { std::cerr << "usage: history <key> [--limit N] --repo <path>\n"; return 2; }
  gitconfig::KVStore kv({a.repo, a.data_root});
  std::string err;
  auto entries = gitconfig::History::list_for_key(kv, a.pos[0], a.limit, &err);
  if (!err.empty()) {
    std::cerr << fmt::format("history failed: {}\n", err);
    return 1;
  }
  for (auto& e : entries) {
    std::cout << fmt::format("{} {}\n", e.sha, e.unix_ts);
  }
  return 0;
}

static int cmd_sync_all(const Args& a) {
  gitconfig::KVStore kv({a.repo, a.data_root});
  gitconfig::Repo r(a.repo);
  std::string err;
  if (!r.ensure_initialized("gitconfig", "gitconfig@localhost", &err)) {
    std::cerr << fmt::format("repo init failed: {}\n", err);
    return 1;
  }
  auto peers = gitconfig::Nodes::list_peers(a.repo, a.data_root);
  std::vector<std::string> remotes;
  if (!a.remote.empty()) remotes.push_back(a.remote);
  for (auto& u : peers) {
    if (!gitconfig::Nodes::ensure_remote_for_peer(r, u, &err)) {}
    remotes.push_back(gitconfig::Nodes::remote_name_for(u));
  }
  std::vector<std::string> synced;
  std::vector<std::string> skipped;
  std::vector<std::pair<std::string,std::string>> errors;
  for (auto& rn : remotes) {
    std::string e2;
    auto out = gitconfig::ConflictResolver::sync_lww(kv, r, rn, a.branch, &e2);
    if (out.ok) {
      synced.push_back(rn);
    } else {
      if (!e2.empty()) errors.push_back({rn, e2});
      else skipped.push_back(rn);
    }
  }
  std::cout << "{";
  std::cout << "\"synced\":[";
  for (size_t i=0;i<synced.size();++i){ std::cout << "\"" << synced[i] << "\""; if (i+1<synced.size()) std::cout << ","; }
  std::cout << "],\"skipped\":[";
  for (size_t i=0;i<skipped.size();++i){ std::cout << "\"" << skipped[i] << "\""; if (i+1<skipped.size()) std::cout << ","; }
  std::cout << "],\"errors\":[";
  for (size_t i=0;i<errors.size();++i){ std::cout << "{\"remote\":\"" << errors[i].first << "\",\"error\":\"" << errors[i].second << "\"}"; if (i+1<errors.size()) std::cout << ","; }
  std::cout << "]}\n";
  return 0;
}

static int cmd_node_add_peer(const Args& a) {
  if (a.url.empty()) { std::cerr << "usage: node add-peer --repo <path> --url <git-url>\n"; return 2; }
  std::string err;
  if (!gitconfig::Nodes::add_peer(a.repo, a.data_root, a.url, &err)) {
    std::cerr << fmt::format("add-peer failed: {}\n", err);
    return 1;
  }
  gitconfig::Repo r(a.repo);
  r.ensure_initialized("gitconfig", "gitconfig@localhost", nullptr);
  if (!gitconfig::Nodes::ensure_remote_for_peer(r, a.url, &err)) {
    std::cerr << fmt::format("remote ensure failed: {}\n", err);
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}

static int cmd_node_list_peers(const Args& a) {
  auto peers = gitconfig::Nodes::list_peers(a.repo, a.data_root);
  for (auto& u : peers) std::cout << u << "\n";
  return 0;
}

static int cmd_serve(const Args& a) {
  router_ns::Cfg cfg{a.repo, a.data_root, a.remote, a.branch};
  router_ns::Router router(cfg);
  server_ns::Server srv(a.addr, a.port, [&](const http_ns::Request& r){ return router.handle(r); });
  sync_ns::SyncLoop loop(&router, a.sync_interval);
  loop.start();
  srv.run();
  loop.stop();
  return 0;
}

static int usage(const char* argv0) {
  std::cerr << fmt::format("usage:\n");
  std::cerr << fmt::format("  {} init --repo <path> [--data-root config]\n", argv0);
  std::cerr << fmt::format("  {} set <key> <value> --repo <path> [--data-root config]\n", argv0);
  std::cerr << fmt::format("  {} get <key> [--commit SHA] [--default VAL] --repo <path> [--data-root config]\n", argv0);
  std::cerr << fmt::format("  {} exists <key> --repo <path> [--data-root config]\n", argv0);
  std::cerr << fmt::format("  {} delete <key> --repo <path> [--data-root config]\n", argv0);
  std::cerr << fmt::format("  {} list [prefix] --repo <path> [--data-root config] [--recursive] [--json]\n", argv0);
  std::cerr << fmt::format("  {} history <key> [--limit N] --repo <path> [--data-root config]\n", argv0);
  std::cerr << fmt::format("  {} remote set --repo <path> --remote <name> --url <url>\n", argv0);
  std::cerr << fmt::format("  {} push --repo <path> [--remote origin] [--branch main]\n", argv0);
  std::cerr << fmt::format("  {} pull --repo <path> [--remote origin] [--branch main]\n", argv0);
  std::cerr << fmt::format("  {} sync --repo <path> [--remote origin] [--branch main]\n", argv0);
  std::cerr << fmt::format("  {} node add-peer --repo <path> --url <git-url>\n", argv0);
  std::cerr << fmt::format("  {} node list-peers --repo <path>\n", argv0);
  std::cerr << fmt::format("  {} serve --repo <path> [--data-root config] [--addr 0.0.0.0] [--port 8080] [--remote origin] [--branch main] [--sync-interval N]\n", argv0);
  return 2;
}

}

int main(int argc, char** argv) {
  using namespace gitconfig_cli;
  auto a = parse(argc, argv);
  if (a.cmd=="init") return cmd_init(a);
  if (a.cmd=="set") return cmd_set(a);
  if (a.cmd=="get") return cmd_get(a);
  if (a.cmd=="exists") return cmd_exists(a);
  if (a.cmd=="delete") return cmd_delete(a);
  if (a.cmd=="list") return cmd_list(a);
  if (a.cmd=="history") return cmd_history(a);
  if (a.cmd=="remote" && !a.pos.empty() && a.pos[0]=="set") return cmd_remote_set(a);
  if (a.cmd=="push") return cmd_push(a);
  if (a.cmd=="pull") return cmd_pull(a);
  if (a.cmd=="sync") return cmd_sync_all(a);
  if (a.cmd=="node" && !a.pos.empty() && a.pos[0]=="add-peer") return cmd_node_add_peer(a);
  if (a.cmd=="node" && !a.pos.empty() && a.pos[0]=="list-peers") return cmd_node_list_peers(a);
  if (a.cmd=="serve") return cmd_serve(a);
  return usage(argv[0]);
}
