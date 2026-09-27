#include "conf.hpp"

#include <cstdlib>
#include <string_view>

namespace yeet::conf {

namespace {

void ReadSeconds(const char *name, std::chrono::seconds &slot) {
  const auto *raw = std::getenv(name);
  if (!raw)
    return;

  if (const long seconds = std::strtol(raw, nullptr, 10); seconds > 0)
    slot = std::chrono::seconds(seconds);
}

} // namespace

[[nodiscard]] Settings InitSettings() {
  Settings settings;

  if (const auto *YEET_DB = std::getenv("YEET_DB"))
    settings.YEET_DB = YEET_DB;

  ReadSeconds("YEET_MAX_SLEEP", settings.YEET_MAX_SLEEP);
  ReadSeconds("YEET_JOB_TIMEOUT", settings.YEET_JOB_TIMEOUT);
  ReadSeconds("YEET_TERM_GRACE", settings.YEET_TERM_GRACE);

  if (const auto *YEET_OUTPUT_CAP = std::getenv("YEET_OUTPUT_CAP")) {
    if (const long bytes = std::strtol(YEET_OUTPUT_CAP, nullptr, 10); bytes > 0)
      settings.YEET_OUTPUT_CAP = static_cast<std::size_t>(bytes);
  }

  if (const auto *YEET_LOG_LEVEL = std::getenv("YEET_LOG_LEVEL")) {
    if (log::Level level; log::ParseLevel(YEET_LOG_LEVEL, level))
      settings.YEET_LOG_LEVEL = level;
  }

  if (const auto *TZ = std::getenv("TZ"))
    settings.TZ = TZ;

  return settings;
}

} // namespace yeet::conf
