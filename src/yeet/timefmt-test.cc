#include <string>

#include <gtest/gtest.h>

#include "yeet/timefmt.hpp"

namespace {

using yeet::schedule::TimePoint;

[[nodiscard]] TimePoint At(int year, unsigned month, unsigned day, int hour,
                           int minute, int second) {
  return std::chrono::sys_days{std::chrono::year{year} /
                               std::chrono::month{month} /
                               std::chrono::day{day}} +
         std::chrono::hours{hour} + std::chrono::minutes{minute} +
         std::chrono::seconds{second};
}

TEST(TimeFmt, FormatsAsSortableUtc) {
  EXPECT_EQ(yeet::timefmt::FormatUtc(At(2026, 9, 26, 5, 4, 3)),
            "2026-09-26 05:04:03");
}

TEST(TimeFmt, RoundTrips) {
  const TimePoint point = At(2026, 2, 28, 23, 59, 59);
  const auto parsed = yeet::timefmt::ParseUtc(yeet::timefmt::FormatUtc(point));

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, point);
}

TEST(TimeFmt, ParsesAsUtcRegardlessOfProcessTimezone) {
  ::setenv("TZ", "America/Lima", 1);
  ::tzset();

  const auto parsed = yeet::timefmt::ParseUtc("2026-09-26 12:00:00");

  ::setenv("TZ", "UTC", 1);
  ::tzset();

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, At(2026, 9, 26, 12, 0, 0));
}

TEST(TimeFmt, RejectsGarbage) {
  EXPECT_FALSE(yeet::timefmt::ParseUtc("").has_value());
  EXPECT_FALSE(yeet::timefmt::ParseUtc("not a time").has_value());
  EXPECT_FALSE(yeet::timefmt::ParseUtc("2026-09-26").has_value());
}

TEST(TimeFmt, SortsLexicographicallyInTimeOrder) {
  EXPECT_LT(yeet::timefmt::FormatUtc(At(2026, 9, 26, 9, 0, 0)),
            yeet::timefmt::FormatUtc(At(2026, 9, 26, 10, 0, 0)));
  EXPECT_LT(yeet::timefmt::FormatUtc(At(2026, 9, 9, 0, 0, 0)),
            yeet::timefmt::FormatUtc(At(2026, 9, 10, 0, 0, 0)));
}

}
