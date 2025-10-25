#include "kv.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <filesystem>

using namespace uringkv;

static void usage(){
  std::cout << "uringkv\n"
            << "Usage:\n"
            << "  uringkv --path DIR [--uring] [--qd N] init\n"
            << "  uringkv --path DIR [--uring] [--qd N] put <key> <value>\n"
            << "  uringkv --path DIR [--uring] [--qd N] get <key>\n"
            << "  uringkv --path DIR [--uring] [--qd N] del <key>\n"
            << "  uringkv --path DIR [--uring] [--qd N] scan <start> <end>\n";
}

int main(int argc, char** argv){
  if (argc < 3) { usage(); return 1; }

  std::string dir;
  bool use_uring=false;
  unsigned qd=256;

  int i=1;
  if (std::string(argv[i]) == "--path" && i+1 < argc) { dir = argv[i+1]; i+=2; }
  else { usage(); return 1; }

  while (i < argc && std::string(argv[i]).rfind("--",0)==0) {
    std::string opt = argv[i];
    if (opt == "--uring") { use_uring = true; ++i; }
    else if (opt == "--qd" && i+1 < argc) { qd = static_cast<unsigned>(std::stoul(argv[i+1])); i+=2; }
    else break;
  }

  KV kv({.path = dir, .use_uring = use_uring, .uring_queue_depth = qd});

  if (i >= argc) { usage(); return 1; }
  std::string cmd = argv[i];

  if (cmd == "init") {
    if (kv.init_storage_layout()) { spdlog::info("initialized at {}", dir); return 0; }
    spdlog::error("failed to init at {}", dir); return 2;
  }
  else if (cmd == "put" && i+2 < argc) {
    bool ok = kv.put(argv[i+1], argv[i+2]);
    std::cout << (ok?"OK":"ERR") << "\n";
    return ok?0:3;
  }
  else if (cmd == "get" && i+1 < argc) {
    auto v = kv.get(argv[i+1]);
    if (v) { std::cout << *v << "\n"; return 0; }
    std::cerr << "NotFound\n"; return 4;
  }
  else if (cmd == "del" && i+1 < argc) {
    bool ok = kv.del(argv[i+1]);
    std::cout << (ok?"OK":"ERR") << "\n";
    return ok?0:5;
  }
  else if (cmd == "scan" && i+2 < argc) {
    auto items = kv.scan(argv[i+1], argv[i+2]);
    for (auto& it: items) std::cout << it.key << "\t" << it.value << "\n";
    return 0;
  }

  usage();
  return 1;
}
