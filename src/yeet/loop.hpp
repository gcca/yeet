#pragma once

#include <string>

#include <sqlite3.h>

#include "yeet/conf.hpp"
#include "yeet/store.hpp"

namespace yeet::loop {

struct Options {
  bool once = false;
};

[[nodiscard]] std::string NewBootId();

struct TickResult {
  int ran = 0;
  bool failed = false;

  std::optional<schedule::TimePoint> earliest;
};

[[nodiscard]] TickResult Tick(sqlite3 *db, const conf::Settings &settings,
                              std::string_view boot_id,
                              schedule::TimePoint now);

[[nodiscard]] int Serve(sqlite3 *db, const conf::Settings &settings,
                        const Options &options);

[[nodiscard]] int TriggerOne(sqlite3 *db, const conf::Settings &settings,
                             std::string_view boot_id, std::string_view name);

}
