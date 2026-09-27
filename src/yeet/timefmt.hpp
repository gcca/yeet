#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "yeet/schedule/spec.hpp"

namespace yeet::timefmt {

[[nodiscard]] std::string FormatUtc(schedule::TimePoint point);
[[nodiscard]] std::optional<schedule::TimePoint>
ParseUtc(std::string_view text);

}
