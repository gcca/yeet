#include <string>

#include <gtest/gtest.h>

#include "yeet/schedule/plan.hpp"

namespace {

using yeet::schedule::Catchup;
using yeet::schedule::Plan;
using yeet::schedule::PlanInput;
using yeet::schedule::Spec;
using yeet::schedule::TimePoint;

// TZ is pinned to UTC by the global environment registered in spec-test.cc.

[[nodiscard]] TimePoint At(int year, unsigned month, unsigned day, int hour,
                           int minute, int second) {
  return std::chrono::sys_days{std::chrono::year{year} /
                               std::chrono::month{month} /
                               std::chrono::day{day}} +
         std::chrono::hours{hour} + std::chrono::minutes{minute} +
         std::chrono::seconds{second};
}

[[nodiscard]] Spec Cron(std::string_view text) {
  Spec spec;
  std::string error;
  EXPECT_TRUE(yeet::schedule::Parse(text, spec, error)) << error;
  return spec;
}

// Defect 1: pulse asks "did a fire land in (now - interval, now]", so a tick
// that runs late either repeats the previous window or skips one. Anchoring on
// the scheduled slot makes the result independent of when the check happens.
TEST(Plan, ALateWakeFiresEachSlotExactlyOnce) {
  const Spec spec = Cron("0 * * * * *");

  PlanInput input;
  input.spec = &spec;
  input.catchup = Catchup::Once;
  input.next_fire_at = At(2026, 9, 26, 12, 0, 0);

  const Plan first =
      yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 0, 37));
  ASSERT_EQ(first.fire.size(), 1u);
  EXPECT_EQ(first.fire.front(), At(2026, 9, 26, 12, 0, 0));
  EXPECT_EQ(first.next_fire_at, At(2026, 9, 26, 12, 1, 0));
  EXPECT_EQ(first.misfired, 0);

  // 82 seconds later, as if collecting children had blocked the loop.
  input.next_fire_at = first.next_fire_at;
  const Plan second =
      yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 1, 59));
  ASSERT_EQ(second.fire.size(), 1u);
  EXPECT_EQ(second.fire.front(), At(2026, 9, 26, 12, 1, 0));
  EXPECT_EQ(second.next_fire_at, At(2026, 9, 26, 12, 2, 0));
}

// Every occurrence in the window is emitted exactly once across an irregular
// sequence of wakeups. This is the invariant pulse violates in both directions.
TEST(Plan, EveryOccurrenceIsEmittedExactlyOnceAcrossRaggedWakeups) {
  const Spec spec = Cron("0 * * * * *");
  const TimePoint start = At(2026, 9, 26, 0, 0, 0);

  PlanInput input;
  input.spec = &spec;
  input.catchup = Catchup::All;
  input.catchup_max = 0;
  input.next_fire_at = yeet::schedule::Seed(spec, start);

  const int offsets[] = {17, 61, 62, 200, 201, 3600, 3601, 7200};
  std::vector<TimePoint> emitted;

  for (const int offset : offsets) {
    const Plan plan =
        yeet::schedule::MakePlan(input, start + std::chrono::seconds{offset});
    emitted.insert(emitted.end(), plan.fire.begin(), plan.fire.end());
    input.next_fire_at = plan.next_fire_at;
    EXPECT_EQ(plan.misfired, 0);
  }

  // 120 minutes elapsed, so minutes 00:01 through 02:00 inclusive.
  ASSERT_EQ(emitted.size(), 120u);
  for (std::size_t i = 0; i < emitted.size(); ++i)
    EXPECT_EQ(emitted[i], start + std::chrono::minutes{static_cast<int>(i) + 1})
        << "at index " << i;
}

// Defect 2: pulse drops missed fires with no record. Run-once-then-resume runs
// the most recent slot and records the rest.
TEST(Plan, AnOutageRunsOnceAndRecordsTheMisses) {
  const Spec spec = Cron("0 0 * * * *");

  PlanInput input;
  input.spec = &spec;
  input.catchup = Catchup::Once;
  input.next_fire_at = At(2026, 9, 26, 9, 0, 0);

  const Plan plan = yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 5, 0));

  ASSERT_EQ(plan.fire.size(), 1u);
  EXPECT_EQ(plan.fire.front(), At(2026, 9, 26, 12, 0, 0));
  EXPECT_EQ(plan.misfired, 3);
  ASSERT_TRUE(plan.first_misfire.has_value());
  EXPECT_EQ(*plan.first_misfire, At(2026, 9, 26, 9, 0, 0));
  EXPECT_EQ(plan.next_fire_at, At(2026, 9, 26, 13, 0, 0));
}

TEST(Plan, SkipPolicyRunsNothingAndCountsEveryMiss) {
  const Spec spec = Cron("0 0 * * * *");

  PlanInput input;
  input.spec = &spec;
  input.catchup = Catchup::Skip;
  input.next_fire_at = At(2026, 9, 26, 9, 0, 0);

  const Plan plan = yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 5, 0));

  EXPECT_TRUE(plan.fire.empty());
  EXPECT_EQ(plan.misfired, 4);
  EXPECT_EQ(plan.next_fire_at, At(2026, 9, 26, 13, 0, 0));
}

TEST(Plan, AllPolicyKeepsTheMostRecentSlotsUpToTheCap) {
  const Spec spec = Cron("0 0 * * * *");

  PlanInput input;
  input.spec = &spec;
  input.catchup = Catchup::All;
  input.catchup_max = 2;
  input.next_fire_at = At(2026, 9, 26, 9, 0, 0);

  const Plan plan = yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 5, 0));

  ASSERT_EQ(plan.fire.size(), 2u);
  EXPECT_EQ(plan.fire[0], At(2026, 9, 26, 11, 0, 0));
  EXPECT_EQ(plan.fire[1], At(2026, 9, 26, 12, 0, 0));
  EXPECT_EQ(plan.misfired, 2);
}

// Defect 3: pulse examines only the first occurrence after the window start,
// so a cron finer than the tick fires once instead of N times.
TEST(Plan, ACronFinerThanTheGapYieldsEverySlotNotOne) {
  const Spec spec = Cron("*/10 * * * * *");

  PlanInput input;
  input.spec = &spec;
  input.catchup = Catchup::All;
  input.catchup_max = 100;
  input.next_fire_at = At(2026, 9, 26, 12, 0, 10);

  const Plan plan = yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 1, 0));

  EXPECT_EQ(plan.fire.size(), 6u);
  EXPECT_EQ(plan.fire.front(), At(2026, 9, 26, 12, 0, 10));
  EXPECT_EQ(plan.fire.back(), At(2026, 9, 26, 12, 1, 0));
  EXPECT_EQ(plan.next_fire_at, At(2026, 9, 26, 12, 1, 10));
}

// Defect 4: an occurrence landing exactly on `now` fires in this pass, and
// only in this pass.
TEST(Plan, AnOccurrenceExactlyAtNowFiresOnceAndNotAgain) {
  const Spec spec = Cron("0 * * * * *");
  const TimePoint now = At(2026, 9, 26, 12, 0, 0);

  PlanInput input;
  input.spec = &spec;
  input.catchup = Catchup::Once;
  input.next_fire_at = now;

  const Plan first = yeet::schedule::MakePlan(input, now);
  ASSERT_EQ(first.fire.size(), 1u);
  EXPECT_EQ(first.fire.front(), now);

  input.next_fire_at = first.next_fire_at;
  const Plan second = yeet::schedule::MakePlan(input, now);
  EXPECT_TRUE(second.fire.empty());
  EXPECT_EQ(second.next_fire_at, first.next_fire_at);
}

TEST(Plan, SeedIsStrictlyInTheFutureSoCreationNeverSelfFires) {
  const Spec spec = Cron("0 * * * * *");
  const TimePoint boundary = At(2026, 9, 26, 12, 0, 0);

  const auto seeded = yeet::schedule::Seed(spec, boundary);

  ASSERT_TRUE(seeded.has_value());
  EXPECT_EQ(*seeded, At(2026, 9, 26, 12, 1, 0));
}

TEST(Plan, AnUnseededJobIsSeededAndFiresNothingYet) {
  const Spec spec = Cron("0 * * * * *");

  PlanInput input;
  input.spec = &spec;
  input.next_fire_at = std::nullopt;

  const Plan plan = yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 0, 30));

  EXPECT_TRUE(plan.fire.empty());
  EXPECT_EQ(plan.next_fire_at, At(2026, 9, 26, 12, 1, 0));
  EXPECT_FALSE(plan.unreachable);
}

TEST(Plan, AJobWithNoScheduleNeverFires) {
  PlanInput input;
  input.spec = nullptr;

  const Plan plan = yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 0, 0));

  EXPECT_TRUE(plan.fire.empty());
  EXPECT_FALSE(plan.next_fire_at.has_value());
}

// Defect 9 in pulse is accidental: a job cannot overlap itself only because
// the tick blocks. Here it is an explicit, recorded decision.
TEST(Plan, AStillRunningJobSkipsItsSlotAndRecordsIt) {
  const Spec spec = Cron("0 * * * * *");

  PlanInput input;
  input.spec = &spec;
  input.running = true;
  input.next_fire_at = At(2026, 9, 26, 12, 0, 0);

  const Plan plan = yeet::schedule::MakePlan(input, At(2026, 9, 26, 12, 0, 30));

  EXPECT_TRUE(plan.fire.empty());
  EXPECT_TRUE(plan.skipped_overlap);
  EXPECT_EQ(plan.misfired, 1);
  EXPECT_EQ(plan.next_fire_at, At(2026, 9, 26, 12, 1, 0));
}

TEST(Plan, SleepForClampsWaitsAndNeverBusySpins) {
  const TimePoint now = At(2026, 9, 26, 12, 0, 0);
  const std::chrono::seconds cap{60};

  EXPECT_EQ(yeet::schedule::SleepFor(At(2026, 9, 26, 18, 0, 0), now, cap), cap);
  EXPECT_EQ(yeet::schedule::SleepFor(At(2026, 9, 26, 12, 0, 15), now, cap),
            std::chrono::seconds{15});
  EXPECT_EQ(yeet::schedule::SleepFor(At(2026, 9, 26, 11, 0, 0), now, cap),
            std::chrono::seconds::zero());
  EXPECT_EQ(yeet::schedule::SleepFor(std::nullopt, now, cap), cap);
}

} // namespace
