#include <cstdlib>
#include <gitproc/cli.hpp>
#include <string_view>

namespace gitproc {

static bool eq(std::string_view a, std::string_view b) { return a == b; }
static bool has_arg(int i, int argc) { return i + 1 < argc; }

static std::optional<RepoUnit> parse_ru(int &i, int argc, char **argv) {
  std::optional<std::string> repo, unit;
  int k = i;
  while (k < argc) {
    std::string_view a = argv[k];
    if (a == "--repo" && has_arg(k, argc)) {
      repo = argv[++k];
    } else if (a == "--unit" && has_arg(k, argc)) {
      unit = argv[++k];
    } else {
      break;
    }
    ++k;
  }
  if (repo && unit) {
    i = k;
    return RepoUnit{*repo, *unit};
  }
  return std::nullopt;
}

ParseResult parse_cli(int argc, char **argv) {
  ParseResult r{};
  if (argc < 2) {
    r.cmd = CmdHelp{};
    return r;
  }

  std::string cmd = argv[1];
  if (eq(cmd, "--help") || eq(cmd, "help")) {
    r.cmd = CmdHelp{};
    return r;
  }
  if (eq(cmd, "--version") || eq(cmd, "version")) {
    r.cmd = CmdVersion{};
    return r;
  }

  auto parse_common = [&](auto ctor) -> ParseResult {
    ParseResult pr{};
    int i = 2;
    if (auto ru = parse_ru(i, argc, argv)) {
      pr.cmd = ctor(ru, std::optional<Target>{});
      return pr;
    }
    if (argc < 3) {
      pr.error = cmd + ": target required";
      return pr;
    }
    pr.cmd = ctor(std::optional<RepoUnit>{}, Target{argv[2]});
    return pr;
  };

  if (cmd == "start")
    return parse_common([](auto ru, auto tgt) { return CmdStart{ru, tgt}; });
  if (cmd == "stop")
    return parse_common([](auto ru, auto tgt) { return CmdStop{ru, tgt}; });
  if (cmd == "status") {
    ParseResult pr{};
    int i = 2;
    auto ru = parse_ru(i, argc, argv);
    CmdStatus c{};
    if (ru)
      c.ru = ru;
    else {
      if (argc < 3) {
        pr.error = "status: target required";
        return pr;
      }
      c.target = Target{argv[2]};
      i = 3;
    }
    for (int k = i; k < argc; k++) {
      if (std::string_view(argv[k]) == "--json")
        c.json = true;
    }
    pr.cmd = c;
    return pr;
  }
  if (cmd == "restart")
    return parse_common([](auto ru, auto tgt) { return CmdRestart{ru, tgt}; });
  if (cmd == "reload")
    return parse_common([](auto ru, auto tgt) { return CmdReload{ru, tgt}; });

  if (cmd == "list") {
    r.cmd = CmdList{};
    return r;
  }

  if (cmd == "logs") {
    if (argc < 3) {
      r.error = "logs: name required";
      return r;
    }
    CmdLogs c{argv[2], false, 100};
    for (int i = 3; i < argc; i++) {
      std::string_view a = argv[i];
      if (a == "--follow")
        c.follow = true;
      else if (a == "--lines" && has_arg(i, argc))
        c.lines = std::atoi(argv[++i]);
    }
    r.cmd = c;
    return r;
  }

  if (cmd == "init") {
    if (argc < 3) {
      r.error = "init: --repo <path|url> required";
      return r;
    }
    r.cmd = CmdInit{argv[2]};
    return r;
  }
  if (cmd == "daemon") {
    CmdDaemon c{"main", 5};
    r.cmd = c;
    return r;
  }
  if (cmd == "sync") {
    CmdSync c{"main"};
    r.cmd = c;
    return r;
  }

  if (cmd == "run") {
    CmdRun c{"", "", "main", 5};
    for (int i = 2; i < argc; i++) {
      std::string_view a = argv[i];
      if (a == "--repo" && has_arg(i, argc))
        c.repo = argv[++i];
      else if (a == "--unit" && has_arg(i, argc))
        c.unit = argv[++i];
      else if (a == "--branch" && has_arg(i, argc))
        c.branch = argv[++i];
      else if (a == "--autosync-sec" && has_arg(i, argc))
        c.autosync_sec = std::atoi(argv[++i]);
    }
    if (c.repo.empty() || c.unit.empty()) {
      r.error = "run: --repo and --unit required";
      return r;
    }
    r.cmd = c;
    return r;
  }

  if (cmd == "reopen-logs") {
    auto pr =
        parse_common([](auto ru, auto tgt) { return CmdReopenLogs{ru, tgt}; });
    if (!pr.cmd && pr.error.empty())
      pr.error = "reopen-logs: target required";
    return pr;
  }

  r.error = "unknown command: " + cmd;
  return r;
}

} // namespace gitproc
