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
  // The earliest scheduled instant across all jobs, which is what the loop
  // sleeps until instead of polling on a fixed period.
  std::optional<schedule::TimePoint> earliest;
};

// One pass: seed unscheduled jobs, run everything due, record misfires.
[[nodiscard]] TickResult Tick(sqlite3 *db, const conf::Settings &settings,
                              std::string_view boot_id,
                              schedule::TimePoint now);

[[nodiscard]] int Serve(sqlite3 *db, const conf::Settings &settings,
                        const Options &options);

[[nodiscard]] int TriggerOne(sqlite3 *db, const conf::Settings &settings,
                             std::string_view boot_id, std::string_view name);

} // namespace yeet::loop
