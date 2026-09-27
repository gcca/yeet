#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "yeet/schedule/spec.hpp"

namespace yeet::schedule {

enum class Catchup { Skip, Once, All };

struct PlanInput {
  const Spec *spec = nullptr;
  std::optional<TimePoint> next_fire_at;
  bool running = false;
  Catchup catchup = Catchup::Once;
  int catchup_max = 10;
};

struct Plan {
  std::vector<TimePoint> fire;
  int misfired = 0;
  std::optional<TimePoint> first_misfire;
  std::optional<TimePoint> next_fire_at;
  bool skipped_overlap = false;
  bool unreachable = false;
};

[[nodiscard]] std::optional<TimePoint> Seed(const Spec &spec, TimePoint now);

[[nodiscard]] Plan MakePlan(const PlanInput &input, TimePoint now);

[[nodiscard]] std::chrono::seconds SleepFor(std::optional<TimePoint> earliest,
                                            TimePoint now,
                                            std::chrono::seconds max_sleep);

}
