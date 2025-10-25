#pragma once
#include <string>
#include <vector>
#include <optional>
#include <variant>

namespace gitproc {

// Универсальная пара --repo/--unit для команд
struct RepoUnit {
  std::string repo;  // путь|URL
  std::string unit;  // относительный путь внутри repo (напр., services/web.unit)
};

// Поддерживаем и старый таргет (name|path) — обратная совместимость
struct Target {
  std::string value;
};

struct CmdStart   { std::optional<RepoUnit> ru; std::optional<Target> target; };
struct CmdStop    { std::optional<RepoUnit> ru; std::optional<Target> target; };
struct CmdStatus  { std::optional<RepoUnit> ru; std::optional<Target> target; std::optional<bool> json = false; };

// Новые формы c --repo/--unit
struct CmdRestart { std::optional<RepoUnit> ru; std::optional<Target> target; };
struct CmdReload  { std::optional<RepoUnit> ru; std::optional<Target> target; };

struct CmdList    { bool all = true; };
struct CmdLogs    { std::string name; bool follow=false; int lines=100; };

struct CmdInit    { std::string repo; };      // заглушка
struct CmdDaemon  { std::string branch; int autosync_sec=5; }; // заглушка
struct CmdSync    { std::string branch; };    // заглушка

struct CmdRun     { std::string repo; std::string unit; std::string branch="main"; int autosync_sec=5; };

// Новая команда: грейс-рестарт ради переоткрытия логов
struct CmdReopenLogs { std::optional<RepoUnit> ru; std::optional<Target> target; };

struct CmdHelp    {};
struct CmdVersion {};

using Command = std::variant<
  CmdStart, CmdStop, CmdStatus, CmdRestart, CmdReload, CmdList, CmdLogs,
  CmdInit, CmdDaemon, CmdSync, CmdRun, CmdReopenLogs, CmdHelp, CmdVersion>;

struct ParseResult {
  std::optional<Command> cmd;
  std::string error;
};

ParseResult parse_cli(int argc, char** argv);

} // namespace gitproc
