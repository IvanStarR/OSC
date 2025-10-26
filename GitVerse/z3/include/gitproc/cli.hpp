#pragma once
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gitproc {

struct RepoUnit {
  std::string repo;
  std::string unit;
};

struct Target {
  std::string value;
};

struct CmdStart {
  std::optional<RepoUnit> ru;
  std::optional<Target> target;
};
struct CmdStop {
  std::optional<RepoUnit> ru;
  std::optional<Target> target;
};
struct CmdStatus {
  std::optional<RepoUnit> ru;
  std::optional<Target> target;
  std::optional<bool> json = false;
};

struct CmdRestart {
  std::optional<RepoUnit> ru;
  std::optional<Target> target;
};
struct CmdReload {
  std::optional<RepoUnit> ru;
  std::optional<Target> target;
};

struct CmdList {
  bool all = true;
};
struct CmdLogs {
  std::string name;
  bool follow = false;
  int lines = 100;
};

struct CmdInit {
  std::string repo;
}; 
struct CmdDaemon {
  std::string branch;
  int autosync_sec = 5;
}; 
struct CmdSync {
  std::string branch;
}; 
struct CmdRun {
  std::string repo;
  std::string unit;
  std::string branch = "main";
  int autosync_sec = 5;
};

struct CmdReopenLogs {
  std::optional<RepoUnit> ru;
  std::optional<Target> target;
};

struct CmdHelp {};
struct CmdVersion {};

using Command =
    std::variant<CmdStart, CmdStop, CmdStatus, CmdRestart, CmdReload, CmdList,
                 CmdLogs, CmdInit, CmdDaemon, CmdSync, CmdRun, CmdReopenLogs,
                 CmdHelp, CmdVersion>;

struct ParseResult {
  std::optional<Command> cmd;
  std::string error;
};

ParseResult parse_cli(int argc, char **argv);

} // namespace gitproc
