#include <cstdlib>
#include <ctime>
#include <string>

#include <gtest/gtest.h>

#include "yeet/schedule/spec.hpp"

namespace {

using yeet::schedule::Spec;
using yeet::schedule::TimePoint;

// croncpp converts through mktime/localtime_r, so every expression is
// evaluated in the process timezone. Pinning it to UTC here is what makes the
// expected instants below absolute rather than dependent on the host.
class UtcEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    ::setenv("TZ", "UTC", 1);
    ::tzset();
  }
};

const ::testing::Environment *const utc =
    ::testing::AddGlobalTestEnvironment(new UtcEnvironment);

[[nodiscard]] TimePoint At(int year, unsigned month, unsigned day, int hour,
                           int minute, int second) {
  return std::chrono::sys_days{std::chrono::year{year} /
                               std::chrono::month{month} /
                               std::chrono::day{day}} +
         std::chrono::hours{hour} + std::chrono::minutes{minute} +
         std::chrono::seconds{second};
}

[[nodiscard]] std::string NormalizedOf(std::string_view text) {
  std::string normalized;
  std::string error;
  EXPECT_TRUE(yeet::schedule::Normalize(text, normalized, error)) << error;
  return normalized;
}

TEST(Spec, FiveFieldsGainASecondsColumn) {
  EXPECT_EQ(NormalizedOf("*/5 * * * *"), "0 */5 * * * *");
  EXPECT_EQ(NormalizedOf("0 3 * * *"), "0 0 3 * * *");
}

TEST(Spec, SixAndSevenFieldsPassThroughWithWhitespaceCollapsed) {
  EXPECT_EQ(NormalizedOf("0 0 3 * * *"), "0 0 3 * * *");
  EXPECT_EQ(NormalizedOf("0  0   3 * * * 2027"), "0 0 3 * * * 2027");
}

TEST(Spec, MacrosExpand) {
  EXPECT_EQ(NormalizedOf("@hourly"), "0 0 * * * *");
  EXPECT_EQ(NormalizedOf("@daily"), "0 0 0 * * *");
  EXPECT_EQ(NormalizedOf("@midnight"), "0 0 0 * * *");
  EXPECT_EQ(NormalizedOf("@weekly"), "0 0 0 * * 0");
  EXPECT_EQ(NormalizedOf("@monthly"), "0 0 0 1 * *");
  EXPECT_EQ(NormalizedOf("@yearly"), "0 0 0 1 1 *");
}

TEST(Spec, UnsupportedMacroAndBadArityAreRejected) {
  std::string normalized;
  std::string error;

  EXPECT_FALSE(yeet::schedule::Normalize("@reboot", normalized, error));
  EXPECT_NE(error.find("@reboot"), std::string::npos);

  EXPECT_FALSE(yeet::schedule::Normalize("* * * *", normalized, error));
  EXPECT_FALSE(yeet::schedule::Normalize("", normalized, error));
  EXPECT_FALSE(yeet::schedule::Normalize("* * * * * * * *", normalized, error));
}

TEST(Spec, ParseRejectsGarbageWithTheLibraryMessage) {
  Spec spec;
  std::string error;

  EXPECT_FALSE(yeet::schedule::Parse("nonsense here now ok go", spec, error));
  EXPECT_FALSE(error.empty());
}

TEST(Spec, NextAfterIsStrictlyAfterTheGivenInstant) {
  Spec spec;
  std::string error;
  ASSERT_TRUE(yeet::schedule::Parse("0 * * * * *", spec, error)) << error;

  const TimePoint on_boundary = At(2026, 9, 26, 12, 0, 0);
  const auto next = yeet::schedule::NextAfter(spec, on_boundary);

  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(*next, At(2026, 9, 26, 12, 1, 0));
}

TEST(Spec, NextAfterEvaluatesInUtc) {
  Spec spec;
  std::string error;
  ASSERT_TRUE(yeet::schedule::Parse("0 30 2 * * *", spec, error)) << error;

  const auto next = yeet::schedule::NextAfter(spec, At(2026, 9, 26, 0, 0, 0));

  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(*next, At(2026, 9, 26, 2, 30, 0));
}

// This croncpp fork carries has_reachable_date, so a date that can never
// occur is rejected when the expression is parsed rather than silently never
// firing at run time.
TEST(Spec, AnImpossibleDateIsRejectedAtParseTime) {
  Spec spec;
  std::string error;

  EXPECT_FALSE(yeet::schedule::Parse("0 0 0 30 2 *", spec, error));
  EXPECT_NE(error.find("invalid"), std::string::npos) << error;
}

// A year field entirely in the past parses, but has no occurrence ahead of
// the given instant, which must surface as an empty optional rather than as
// croncpp's time_point::min() sentinel.
TEST(Spec, AnExhaustedScheduleYieldsNullopt) {
  Spec spec;
  std::string error;
  ASSERT_TRUE(yeet::schedule::Parse("0 0 0 1 1 * 2020", spec, error)) << error;

  EXPECT_FALSE(
      yeet::schedule::NextAfter(spec, At(2026, 1, 1, 0, 0, 0)).has_value());
}

} // namespace
