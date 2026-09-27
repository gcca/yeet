#include <cstdlib>
#include <ctime>
#include <string>

#include <CLI11.hpp>

#include "yeet/conf.hpp"
#include "yeet/db.hpp"
#include "yeet/log.hpp"
#include "yeet/loop.hpp"

int main(int argc, char *argv[]) {
  yeet::conf::Settings settings = yeet::conf::InitSettings();

  CLI::App app{"Run programs from a sqlite3 table on a cron schedule"};
  app.require_subcommand(1);
  app.fallthrough();

  app.add_option("-d,--db", settings.YEET_DB, "Path to the yeet database")
      ->capture_default_str();

  std::string log_level{yeet::log::LevelName(settings.YEET_LOG_LEVEL)};
  app.add_option("--log-level", log_level, "debug, info, warn or error")
      ->check(CLI::IsMember({"debug", "info", "warn", "error"}))
      ->capture_default_str();

  yeet::loop::Options options;
  CLI::App *run =
      app.add_subcommand("run", "Run the scheduler in the foreground");
  run->add_flag("--once", options.once, "Run one pass, then exit");

  std::string trigger_name;
  CLI::App *trigger =
      app.add_subcommand("trigger", "Run one job now, ignoring its schedule");
  trigger->add_option("name", trigger_name, "Job name")->required();

  CLI11_PARSE(app, argc, argv);

  if (yeet::log::Level level; yeet::log::ParseLevel(log_level, level))
    yeet::log::SetLevel(level);

  ::setenv("TZ", settings.TZ.c_str(), 1);
  ::tzset();

  std::string error;
  sqlite3 *db = yeet::db::Connect(settings.YEET_DB, error);
  if (db == nullptr) {
    yeet::log::Error("{}", error);
    return 1;
  }

  if (!yeet::db::RequireSchema(db, error)) {
    yeet::log::Error("{}", error);
    sqlite3_close(db);
    return 1;
  }

  int status = 1;

  if (run->parsed())
    status = yeet::loop::Serve(db, settings, options);
  else if (trigger->parsed())
    status = yeet::loop::TriggerOne(db, settings, yeet::loop::NewBootId(),
                                    trigger_name);

  sqlite3_close(db);

  return status;
}
