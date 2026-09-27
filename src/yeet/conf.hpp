#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "yeet/log.hpp"

namespace yeet::conf {

struct Settings {
  std::string YEET_DB = "data/yeet.db";
  std::chrono::seconds YEET_MAX_SLEEP = std::chrono::seconds(60);
  std::chrono::seconds YEET_JOB_TIMEOUT = std::chrono::seconds(300);
  std::chrono::seconds YEET_TERM_GRACE = std::chrono::seconds(10);
  std::size_t YEET_OUTPUT_CAP = 65536;
  log::Level YEET_LOG_LEVEL = log::Level::Info;
  std::string TZ = "UTC";
};

[[nodiscard]] Settings InitSettings();

}
