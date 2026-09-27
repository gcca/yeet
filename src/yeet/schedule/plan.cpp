#include "plan.hpp"

namespace yeet::schedule {

namespace {

constexpr int MaxCatchupScan = 100000;

}

[[nodiscard]] std::optional<TimePoint> Seed(const Spec &spec, TimePoint now) {
  return NextAfter(spec, now);
}

[[nodiscard]] Plan MakePlan(const PlanInput &input, TimePoint now) {
  Plan plan;

  if (input.spec == nullptr) {
    plan.next_fire_at = std::nullopt;
    return plan;
  }

  if (!input.next_fire_at.has_value()) {
    plan.next_fire_at = Seed(*input.spec, now);
    plan.unreachable = !plan.next_fire_at.has_value();
    return plan;
  }

  std::vector<TimePoint> due;
  TimePoint slot = *input.next_fire_at;
  std::optional<TimePoint> cursor = slot;
  int scanned = 0;

  while (cursor.has_value() && *cursor <= now && scanned < MaxCatchupScan) {
    due.push_back(*cursor);
    cursor = NextAfter(*input.spec, *cursor);
    ++scanned;
  }

  if (due.empty()) {
    plan.next_fire_at = input.next_fire_at;
    return plan;
  }

  plan.next_fire_at = cursor;
  plan.unreachable = !cursor.has_value();
  plan.first_misfire = due.front();

  if (input.running) {
    plan.skipped_overlap = true;
    plan.misfired = static_cast<int>(due.size());
    return plan;
  }

  switch (input.catchup) {
  case Catchup::Skip:
    plan.misfired = static_cast<int>(due.size());
    break;

  case Catchup::Once:
    plan.fire.push_back(due.back());
    plan.misfired = static_cast<int>(due.size()) - 1;
    break;

  case Catchup::All: {
    const auto limit =
        static_cast<std::size_t>(input.catchup_max > 0 ? input.catchup_max : 0);

    if (limit == 0 || due.size() <= limit) {
      plan.fire = due;
      plan.misfired = 0;
    } else {

      plan.fire.assign(due.end() - static_cast<std::ptrdiff_t>(limit),
                       due.end());
      plan.misfired = static_cast<int>(due.size() - limit);
    }
    break;
  }
  }

  if (plan.misfired == 0)
    plan.first_misfire = std::nullopt;

  return plan;
}

[[nodiscard]] std::chrono::seconds SleepFor(std::optional<TimePoint> earliest,
                                            TimePoint now,
                                            std::chrono::seconds max_sleep) {
  if (!earliest.has_value())
    return max_sleep;

  if (*earliest <= now)
    return std::chrono::seconds::zero();

  const auto wait =
      std::chrono::duration_cast<std::chrono::seconds>(*earliest - now);

  return wait < max_sleep ? wait : max_sleep;
}

}
