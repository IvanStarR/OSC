// application/main.cpp
#include "kv.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace uringkv;

namespace {

void usage() {
  std::cout
      << "uringkv - minimal KV over WAL+SST\n\n"
      << "Usage:\n"
      << "  uringkv --path DIR [--uring] [--qd N] [--wal-max MB] [--flush MB] "
         "init\n"
      << "  uringkv --path DIR [--uring] [--qd N] [--wal-max MB] [--flush MB] "
         "put <key> <value>\n"
      << "  uringkv --path DIR [--uring] [--qd N] [--wal-max MB] [--flush MB] "
         "get <key>\n"
      << "  uringkv --path DIR [--uring] [--qd N] [--wal-max MB] [--flush MB] "
         "del <key>\n"
      << "  uringkv --path DIR [--uring] [--qd N] [--wal-max MB] [--flush MB] "
         "scan <start> <end>\n"
      << "\n"
      << "Options:\n"
      << "  --path DIR     Базовый каталог для данных (обязателен)\n"
      << "  --uring        Включить io_uring (если доступен в системе)\n"
      << "  --qd N         Глубина очереди io_uring (по умолчанию 256)\n"
      << "  --wal-max MB   Максимальный размер сегмента WAL (по умолчанию 64)\n"
      << "  --flush MB     Порог флаша MemTable -> SST (по умолчанию 4)\n"
      << "  --help         Показать эту справку\n";
}

bool is_flag(const char *s, const char *name) {
  return std::strcmp(s, name) == 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 1 ||
      (argc > 1 && (is_flag(argv[1], "-h") || is_flag(argv[1], "--help")))) {
    usage();
    return 0;
  }

  std::string dir;
  bool use_uring = false;
  unsigned qd = 256;
  uint64_t wal_max_mb = 64;
  uint64_t flush_mb = 4;

  // Простой линейный парсер опций
  int i = 1;
  if (i < argc && is_flag(argv[i], "--path")) {
    if (i + 1 >= argc) {
      std::cerr << "--path требует аргумент\n";
      usage();
      return 1;
    }
    dir = argv[i + 1];
    i += 2;
  } else {
    std::cerr << "Ошибка: требуется указать --path DIR\n";
    usage();
    return 1;
  }

  while (i < argc && std::strncmp(argv[i], "--", 2) == 0) {
    if (is_flag(argv[i], "--uring")) {
      use_uring = true;
      ++i;
    } else if (is_flag(argv[i], "--qd")) {
      if (i + 1 >= argc) {
        std::cerr << "--qd требует число\n";
        return 1;
      }
      qd = static_cast<unsigned>(std::stoul(argv[i + 1]));
      i += 2;
    } else if (is_flag(argv[i], "--wal-max")) {
      if (i + 1 >= argc) {
        std::cerr << "--wal-max требует число (MB)\n";
        return 1;
      }
      wal_max_mb = std::stoull(argv[i + 1]);
      i += 2;
    } else if (is_flag(argv[i], "--flush")) {
      if (i + 1 >= argc) {
        std::cerr << "--flush требует число (MB)\n";
        return 1;
      }
      flush_mb = std::stoull(argv[i + 1]);
      i += 2;
    } else if (is_flag(argv[i], "--help")) {
      usage();
      return 0;
    } else {
      // неизвестный флаг — выходим из цикла, дальше ожидаем команду
      break;
    }
  }

  if (i >= argc) {
    std::cerr << "Не указана команда (init/put/get/del/scan)\n";
    usage();
    return 1;
  }

  const std::string cmd = argv[i++];

  // Инициализируем движок с опциями
  KV kv({.path = dir,
         .use_uring = use_uring,
         .uring_queue_depth = qd,
         .wal_max_segment_bytes = wal_max_mb * 1024ull * 1024ull,
         .sst_flush_threshold_bytes = flush_mb * 1024ull * 1024ull});

  // Команды
  if (cmd == "init") {
    const bool ok = kv.init_storage_layout();
    if (ok) {
      spdlog::info("Initialized at {}", dir);
      return 0;
    }
    spdlog::error("Failed to init at {}", dir);
    return 2;
  }

  if (cmd == "put") {
    if (i + 1 >= argc) {
      std::cerr << "put требует <key> <value>\n";
      return 1;
    }
    const char *key = argv[i];
    const char *val = argv[i + 1];
    const bool ok = kv.put(key, val);
    std::cout << (ok ? "OK" : "ERR") << "\n";
    return ok ? 0 : 3;
  }

  if (cmd == "get") {
    if (i >= argc) {
      std::cerr << "get требует <key>\n";
      return 1;
    }
    const char *key = argv[i];
    auto v = kv.get(key);
    if (v) {
      std::cout << *v << "\n";
      return 0;
    }
    std::cerr << "NotFound\n";
    return 4;
  }

  if (cmd == "del") {
    if (i >= argc) {
      std::cerr << "del требует <key>\n";
      return 1;
    }
    const char *key = argv[i];
    const bool ok = kv.del(key);
    std::cout << (ok ? "OK" : "ERR") << "\n";
    return ok ? 0 : 5;
  }

  if (cmd == "scan") {
    if (i + 1 >= argc) {
      std::cerr << "scan требует <start> <end>\n";
      return 1;
    }
    const char *start = argv[i];
    const char *end = argv[i + 1];
    auto items = kv.scan(start, end);
    for (auto &it : items) {
      std::cout << it.key << "\t" << it.value << "\n";
    }
    return 0;
  }

  std::cerr << "Неизвестная команда: " << cmd << "\n";
  usage();
  return 1;
}
