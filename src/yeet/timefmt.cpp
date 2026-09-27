#include "timefmt.hpp"

#include <cstdio>
#include <ctime>
#include <format>

namespace yeet::timefmt {

[[nodiscard]] std::string FormatUtc(schedule::TimePoint point) {
  return std::format("{:%Y-%m-%d %H:%M:%S}", point);
}

[[nodiscard]] std::optional<schedule::TimePoint>
ParseUtc(std::string_view text) {
  std::tm parts{};
  const std::string owned(text);

  if (std::sscanf(owned.c_str(), "%4d-%2d-%2d %2d:%2d:%2d", &parts.tm_year,
                  &parts.tm_mon, &parts.tm_mday, &parts.tm_hour, &parts.tm_min,
                  &parts.tm_sec) != 6)
    return std::nullopt;

  parts.tm_year -= 1900;
  parts.tm_mon -= 1;
  parts.tm_isdst = 0;

  const std::time_t seconds = ::timegm(&parts);
  if (seconds == static_cast<std::time_t>(-1))
    return std::nullopt;

  return std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::from_time_t(seconds));
}

}
