#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "yeet/conf.hpp"

namespace {

// Every test mutates the process environment, so each variable it touches is
// restored afterwards; otherwise ordering between tests would decide results.
class ScopedEnv {
public:
  ScopedEnv(std::string name, const char *value) : name_(std::move(name)) {
    if (const auto *previous = std::getenv(name_.c_str())) {
      had_previous_ = true;
      previous_ = previous;
    }

    if (value)
      ::setenv(name_.c_str(), value, 1);
    else
      ::unsetenv(name_.c_str());
  }

  ScopedEnv(const ScopedEnv &) = delete;
  ScopedEnv &operator=(const ScopedEnv &) = delete;

  ~ScopedEnv() {
    if (had_previous_)
      ::setenv(name_.c_str(), previous_.c_str(), 1);
    else
      ::unsetenv(name_.c_str());
  }

private:
  std::string name_;
  std::string previous_;
  bool had_previous_ = false;
};

TEST(Conf, DefaultsApplyWhenTheEnvironmentIsEmpty) {
  const ScopedEnv db("YEET_DB", nullptr);
  const ScopedEnv sleep("YEET_MAX_SLEEP", nullptr);
  const ScopedEnv timeout("YEET_JOB_TIMEOUT", nullptr);
  const ScopedEnv cap("YEET_OUTPUT_CAP", nullptr);
  const ScopedEnv level("YEET_LOG_LEVEL", nullptr);

  const auto settings = yeet::conf::InitSettings();

  EXPECT_EQ(settings.YEET_DB, "data/yeet.db");
  EXPECT_EQ(settings.YEET_MAX_SLEEP, std::chrono::seconds(60));
  EXPECT_EQ(settings.YEET_JOB_TIMEOUT, std::chrono::seconds(300));
  EXPECT_EQ(settings.YEET_OUTPUT_CAP, 65536u);
  EXPECT_EQ(settings.YEET_LOG_LEVEL, yeet::log::Level::Info);
}

TEST(Conf, EnvironmentOverridesEveryField) {
  const ScopedEnv db("YEET_DB", "/tmp/other.db");
  const ScopedEnv sleep("YEET_MAX_SLEEP", "15");
  const ScopedEnv timeout("YEET_JOB_TIMEOUT", "42");
  const ScopedEnv cap("YEET_OUTPUT_CAP", "1024");
  const ScopedEnv level("YEET_LOG_LEVEL", "warn");

  const auto settings = yeet::conf::InitSettings();

  EXPECT_EQ(settings.YEET_DB, "/tmp/other.db");
  EXPECT_EQ(settings.YEET_MAX_SLEEP, std::chrono::seconds(15));
  EXPECT_EQ(settings.YEET_JOB_TIMEOUT, std::chrono::seconds(42));
  EXPECT_EQ(settings.YEET_OUTPUT_CAP, 1024u);
  EXPECT_EQ(settings.YEET_LOG_LEVEL, yeet::log::Level::Warn);
}

TEST(Conf, NonPositiveAndUnparseableValuesKeepTheDefault) {
  const ScopedEnv sleep("YEET_MAX_SLEEP", "0");
  const ScopedEnv timeout("YEET_JOB_TIMEOUT", "-5");
  const ScopedEnv cap("YEET_OUTPUT_CAP", "not-a-number");
  const ScopedEnv level("YEET_LOG_LEVEL", "verbose");

  const auto settings = yeet::conf::InitSettings();

  EXPECT_EQ(settings.YEET_MAX_SLEEP, std::chrono::seconds(60));
  EXPECT_EQ(settings.YEET_JOB_TIMEOUT, std::chrono::seconds(300));
  EXPECT_EQ(settings.YEET_OUTPUT_CAP, 65536u);
  EXPECT_EQ(settings.YEET_LOG_LEVEL, yeet::log::Level::Info);
}

TEST(Conf, InitSettingsDoesNotTouchTheFilesystem) {
  const ScopedEnv db("YEET_DB", "/tmp/yeet-conf-test-should-not-exist/x.db");

  const auto settings = yeet::conf::InitSettings();

  EXPECT_EQ(settings.YEET_DB, "/tmp/yeet-conf-test-should-not-exist/x.db");
  EXPECT_FALSE(std::filesystem::exists("/tmp/yeet-conf-test-should-not-exist"));
}

} // namespace
