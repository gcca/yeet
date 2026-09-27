#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "yeet/schedule/spec.hpp"

namespace yeet::timefmt {

// One formatter/parser pair for the whole daemon, so every TEXT timestamp in
// the database has the same shape and sorts lexicographically.
[[nodiscard]] std::string FormatUtc(schedule::TimePoint point);
[[nodiscard]] std::optional<schedule::TimePoint>
ParseUtc(std::string_view text);

} // namespace yeet::timefmt
