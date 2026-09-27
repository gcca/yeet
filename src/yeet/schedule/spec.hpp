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

[[nodiscard]] bool Normalize(std::string_view text, std::string &normalized,
                             std::string &error);

[[nodiscard]] bool Parse(std::string_view text, Spec &spec, std::string &error);

[[nodiscard]] std::optional<TimePoint> NextAfter(const Spec &spec,
                                                 TimePoint from);

}
