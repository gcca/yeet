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

// The first occurrence strictly after `now`, which is what a newly created or
// rescheduled job starts from.
[[nodiscard]] std::optional<TimePoint> Seed(const Spec &spec, TimePoint now);

// Consumes every due slot and advances the schedule from the last scheduled
// instant, never from `now` and never from when a run finished. That anchoring
// is what keeps the emitted sequence equal to the cron series no matter how
// late or how often this is called.
[[nodiscard]] Plan MakePlan(const PlanInput &input, TimePoint now);

[[nodiscard]] std::chrono::seconds SleepFor(std::optional<TimePoint> earliest,
                                            TimePoint now,
                                            std::chrono::seconds max_sleep);

} // namespace yeet::schedule
