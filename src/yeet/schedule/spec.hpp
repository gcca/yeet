#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <croncpp.h>

namespace yeet::schedule {

using TimePoint = std::chrono::sys_seconds;

struct Spec {
  ::cron::cronexpr expr;
  std::string normalized;
};

// Expands the @-macros croncpp does not implement and pads a 5-field POSIX
// expression to the 6 fields croncpp requires.
[[nodiscard]] bool Normalize(std::string_view text, std::string &normalized,
                             std::string &error);

[[nodiscard]] bool Parse(std::string_view text, Spec &spec, std::string &error);

// Strictly after `from`. Empty when the expression has no reachable
// occurrence, which croncpp signals with time_point::min().
[[nodiscard]] std::optional<TimePoint> NextAfter(const Spec &spec,
                                                 TimePoint from);

} // namespace yeet::schedule
