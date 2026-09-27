#include <string>

#include <gtest/gtest.h>

#include "yeet/log.hpp"

namespace {

using yeet::log::Level;

TEST(Log, ParseLevelAcceptsEveryName) {
  Level level = Level::Error;

  ASSERT_TRUE(yeet::log::ParseLevel("debug", level));
  EXPECT_EQ(level, Level::Debug);
  ASSERT_TRUE(yeet::log::ParseLevel("info", level));
  EXPECT_EQ(level, Level::Info);
  ASSERT_TRUE(yeet::log::ParseLevel("warn", level));
  EXPECT_EQ(level, Level::Warn);
  ASSERT_TRUE(yeet::log::ParseLevel("error", level));
  EXPECT_EQ(level, Level::Error);
}

TEST(Log, ParseLevelRejectsUnknownAndLeavesTargetUntouched) {
  Level level = Level::Warn;

  EXPECT_FALSE(yeet::log::ParseLevel("INFO", level));
  EXPECT_FALSE(yeet::log::ParseLevel("", level));
  EXPECT_FALSE(yeet::log::ParseLevel("trace", level));
  EXPECT_EQ(level, Level::Warn);
}

TEST(Log, LevelNameRoundTrips) {
  for (const Level level :
       {Level::Debug, Level::Info, Level::Warn, Level::Error}) {
    Level parsed = Level::Error;
    ASSERT_TRUE(yeet::log::ParseLevel(yeet::log::LevelName(level), parsed));
    EXPECT_EQ(parsed, level);
  }
}

TEST(Log, FormatLinePadsTheLevelAndKeepsFieldOrder) {
  EXPECT_EQ(yeet::log::FormatLine(Level::Info, "2026-09-26T00:00:00Z", "ran x"),
            "2026-09-26T00:00:00Z info  ran x");
  EXPECT_EQ(yeet::log::FormatLine(Level::Error, "2026-09-26T00:00:00Z", "boom"),
            "2026-09-26T00:00:00Z error boom");
}

TEST(Log, SetLevelIsObservable) {
  const Level restore = yeet::log::CurrentLevel();

  yeet::log::SetLevel(Level::Error);
  EXPECT_EQ(yeet::log::CurrentLevel(), Level::Error);
  yeet::log::SetLevel(Level::Debug);
  EXPECT_EQ(yeet::log::CurrentLevel(), Level::Debug);

  yeet::log::SetLevel(restore);
}

}
