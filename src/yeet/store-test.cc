#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "yeet/db.hpp"
#include "yeet/store.hpp"
#include "yeet/timefmt.hpp"

namespace {

using yeet::schedule::TimePoint;
using yeet::store::JobRow;

[[nodiscard]] TimePoint At(int year, unsigned month, unsigned day, int hour,
                           int minute, int second) {
  return std::chrono::sys_days{std::chrono::year{year} /
                               std::chrono::month{month} /
                               std::chrono::day{day}} +
         std::chrono::hours{hour} + std::chrono::minutes{minute} +
         std::chrono::seconds{second};
}

class StoreTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(sqlite3_open(":memory:", &db_), SQLITE_OK);

    std::ifstream schema{YEET_SCHEMA_SQL};
    ASSERT_TRUE(schema.is_open())
        << "missing " << YEET_SCHEMA_SQL
        << " -- regenerate with: yeet-initdb --print-schema > db/schema.sql";

    std::ostringstream buffer;
    buffer << schema.rdbuf();

    std::string error;
    ASSERT_TRUE(yeet::db::Exec(db_, buffer.str().c_str(), error)) << error;
    ASSERT_TRUE(yeet::db::Exec(db_, "PRAGMA foreign_keys = ON", error))
        << error;
  }

  void TearDown() override { sqlite3_close(db_); }

  void Add(const char *name, const char *command, const char *args,
           const char *cron) {
    const std::string sql =
        std::string("INSERT INTO job (name, command, args, cron) VALUES ('") +
        name + "', '" + command + "', '" + args + "', " +
        (cron == nullptr ? std::string("NULL")
                         : std::string("'") + cron + "'") +
        ")";

    std::string error;
    ASSERT_TRUE(yeet::db::Exec(db_, sql.c_str(), error)) << error;
  }

  [[nodiscard]] int Count(const char *sql) {
    sqlite3_stmt *stmt = nullptr;
    EXPECT_EQ(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), SQLITE_OK);

    int value = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
      value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return value;
  }

  sqlite3 *db_ = nullptr;
};

TEST_F(StoreTest, TheGeneratedSchemaMatchesTheVersionTheDaemonRequires) {
  std::string error;
  EXPECT_TRUE(yeet::db::RequireSchema(db_, error)) << error;
}

TEST_F(StoreTest, TheInsertTriggerGivesEveryJobAStateRow) {
  Add("a", "/bin/true", "[]", "0 * * * * *");
  Add("b", "/bin/true", "[]", "0 * * * * *");

  EXPECT_EQ(Count("SELECT count(*) FROM job_state"), 2);
}

TEST_F(StoreTest, ArgumentsComeBackAsSqliteParsedThemIncludingSpaces) {
  Add("rsync", "/usr/bin/rsync", R"(["-a","--delete","/src dir","/dst"])",
      "0 * * * * *");

  std::vector<JobRow> jobs;
  std::string error;
  ASSERT_TRUE(yeet::store::LoadSchedulable(db_, jobs, error)) << error;

  ASSERT_EQ(jobs.size(), 1u);
  ASSERT_EQ(jobs[0].args.size(), 4u);
  EXPECT_EQ(jobs[0].args[0], "-a");
  EXPECT_EQ(jobs[0].args[1], "--delete");
  EXPECT_EQ(jobs[0].args[2], "/src dir");
  EXPECT_EQ(jobs[0].args[3], "/dst");
}

TEST_F(StoreTest, ManualOnlyAndDisabledJobsAreNotScheduled) {
  Add("scheduled", "/bin/true", "[]", "0 * * * * *");
  Add("manual", "/bin/true", "[]", nullptr);
  Add("off", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(yeet::db::Exec(
      db_, "UPDATE job SET enabled = 0 WHERE name = 'off'", error))
      << error;

  std::vector<JobRow> jobs;
  ASSERT_TRUE(yeet::store::LoadSchedulable(db_, jobs, error)) << error;

  ASSERT_EQ(jobs.size(), 1u);
  EXPECT_EQ(jobs[0].name, "scheduled");
}

TEST_F(StoreTest, EditingTheCronClearsTheCachedNextFire) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(
      yeet::store::SetNextFire(db_, 1, At(2026, 9, 26, 12, 0, 0), error))
      << error;
  EXPECT_EQ(Count("SELECT count(*) FROM job_state WHERE next_fire_at IS NOT "
                  "NULL"),
            1);

  ASSERT_TRUE(yeet::db::Exec(
      db_, "UPDATE job SET cron = '0 0 * * * *' WHERE id = 1", error))
      << error;

  EXPECT_EQ(Count("SELECT count(*) FROM job_state WHERE next_fire_at IS NULL"),
            1);
}

TEST_F(StoreTest, ClaimingASlotWritesTheRunAndAdvancesTheScheduleTogether) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  const TimePoint slot = At(2026, 9, 26, 12, 0, 0);
  const TimePoint next = At(2026, 9, 26, 12, 1, 0);

  std::string error;
  ASSERT_TRUE(yeet::store::SetNextFire(db_, 1, slot, error)) << error;

  std::vector<JobRow> jobs;
  ASSERT_TRUE(yeet::store::LoadSchedulable(db_, jobs, error)) << error;
  ASSERT_EQ(jobs.size(), 1u);

  std::int64_t run_id = 0;
  bool claimed = false;
  ASSERT_TRUE(yeet::store::ClaimSlot(db_, jobs[0], slot, next, "cron", "boot1",
                                     run_id, claimed, error))
      << error;

  EXPECT_TRUE(claimed);
  EXPECT_GT(run_id, 0);
  EXPECT_EQ(Count("SELECT count(*) FROM run WHERE status = 'running'"), 1);
  EXPECT_EQ(Count("SELECT count(*) FROM job_state WHERE next_fire_at = "
                  "'2026-09-26 12:01:00'"),
            1);
}

TEST_F(StoreTest, AStaleClaimChangesNothing) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  const TimePoint slot = At(2026, 9, 26, 12, 0, 0);

  std::string error;
  ASSERT_TRUE(yeet::store::SetNextFire(db_, 1, slot, error)) << error;

  std::vector<JobRow> jobs;
  ASSERT_TRUE(yeet::store::LoadSchedulable(db_, jobs, error)) << error;

  ASSERT_TRUE(
      yeet::store::SetNextFire(db_, 1, At(2026, 9, 26, 13, 0, 0), error))
      << error;

  std::int64_t run_id = 0;
  bool claimed = false;
  ASSERT_TRUE(yeet::store::ClaimSlot(db_, jobs[0], slot,
                                     At(2026, 9, 26, 12, 1, 0), "cron", "boot1",
                                     run_id, claimed, error))
      << error;

  EXPECT_FALSE(claimed);
  EXPECT_EQ(Count("SELECT count(*) FROM run"), 0);
}

TEST_F(StoreTest, TheSameCronSlotCannotBeRecordedTwice) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(yeet::db::Exec(
      db_,
      "INSERT INTO run (job_id, job_name, command, args, source, status, "
      "scheduled_at, started_at, boot_id) VALUES (1,'a','/bin/true','[]',"
      "'cron','running','2026-09-26 12:00:00','2026-09-26 12:00:00','b')",
      error))
      << error;

  const bool second = yeet::db::Exec(
      db_,
      "INSERT INTO run (job_id, job_name, command, args, source, status, "
      "scheduled_at, started_at, boot_id) VALUES (1,'a','/bin/true','[]',"
      "'catchup','running','2026-09-26 12:00:00','2026-09-26 12:00:00','b')",
      error);

  EXPECT_FALSE(second);
  EXPECT_EQ(Count("SELECT count(*) FROM run"), 1);
}

TEST_F(StoreTest, ManualRunsAreExemptFromTheSlotConstraint) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  for (int i = 0; i < 2; ++i)
    ASSERT_TRUE(yeet::db::Exec(
        db_,
        "INSERT INTO run (job_id, job_name, command, args, source, status, "
        "scheduled_at, started_at, boot_id) VALUES (1,'a','/bin/true','[]',"
        "'manual','running','2026-09-26 12:00:00','2026-09-26 12:00:00','b')",
        error))
        << error;

  EXPECT_EQ(Count("SELECT count(*) FROM run"), 2);
}

TEST_F(StoreTest, AManualClaimLeavesTheScheduleAlone) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(
      yeet::store::SetNextFire(db_, 1, At(2026, 9, 26, 12, 0, 0), error))
      << error;

  JobRow job;
  ASSERT_TRUE(yeet::store::FindByName(db_, "a", job, error)) << error;

  ASSERT_TRUE(
      yeet::store::SetNextFire(db_, 1, At(2026, 9, 26, 13, 0, 0), error))
      << error;

  std::int64_t run_id = 0;
  bool claimed = false;
  ASSERT_TRUE(yeet::store::ClaimManual(db_, job, At(2026, 9, 26, 12, 30, 0),
                                       "boot1", run_id, claimed, error))
      << error;

  EXPECT_TRUE(claimed);
  EXPECT_GT(run_id, 0);
  EXPECT_EQ(Count("SELECT count(*) FROM run WHERE source = 'manual' AND "
                  "status = 'running'"),
            1);
  EXPECT_EQ(Count("SELECT count(*) FROM job_state WHERE next_fire_at = "
                  "'2026-09-26 13:00:00' AND last_fire_at IS NULL"),
            1);
  EXPECT_EQ(Count("SELECT running_run_id FROM job_state"),
            static_cast<int>(run_id));
}

TEST_F(StoreTest, AManualClaimIsRefusedWhileASkipJobIsRunning) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  JobRow job;
  std::string error;
  ASSERT_TRUE(yeet::store::FindByName(db_, "a", job, error)) << error;

  std::int64_t first = 0;
  bool claimed = false;
  ASSERT_TRUE(yeet::store::ClaimManual(db_, job, At(2026, 9, 26, 12, 0, 0),
                                       "boot1", first, claimed, error))
      << error;
  ASSERT_TRUE(claimed);

  std::int64_t second = 0;
  ASSERT_TRUE(yeet::store::ClaimManual(db_, job, At(2026, 9, 26, 12, 0, 0),
                                       "boot1", second, claimed, error))
      << error;

  EXPECT_FALSE(claimed);
  EXPECT_EQ(second, 0);
  EXPECT_EQ(Count("SELECT count(*) FROM run"), 1);
}

TEST_F(StoreTest, ParallelJobsAcceptOverlappingManualClaims) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(yeet::db::Exec(
      db_, "UPDATE job SET overlap_policy = 'parallel' WHERE id = 1", error))
      << error;

  JobRow job;
  ASSERT_TRUE(yeet::store::FindByName(db_, "a", job, error)) << error;

  for (int i = 0; i < 2; ++i) {
    std::int64_t run_id = 0;
    bool claimed = false;
    ASSERT_TRUE(yeet::store::ClaimManual(db_, job, At(2026, 9, 26, 12, 0, 0),
                                         "boot1", run_id, claimed, error))
        << error;
    EXPECT_TRUE(claimed);
  }

  EXPECT_EQ(Count("SELECT count(*) FROM run"), 2);
}

TEST_F(StoreTest, OnlyTheRunHoldingTheMarkerReleasesIt) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(yeet::db::Exec(
      db_, "UPDATE job SET overlap_policy = 'parallel' WHERE id = 1", error))
      << error;

  JobRow job;
  ASSERT_TRUE(yeet::store::FindByName(db_, "a", job, error)) << error;

  std::int64_t first = 0;
  std::int64_t second = 0;
  bool claimed = false;
  ASSERT_TRUE(yeet::store::ClaimManual(db_, job, At(2026, 9, 26, 12, 0, 0),
                                       "boot1", first, claimed, error))
      << error;
  ASSERT_TRUE(yeet::store::ClaimManual(db_, job, At(2026, 9, 26, 12, 0, 0),
                                       "boot1", second, claimed, error))
      << error;

  yeet::exec::Outcome outcome;
  outcome.exited = true;
  outcome.exit_code = 0;

  ASSERT_TRUE(yeet::store::FinishRun(db_, first, job.id, outcome, error))
      << error;
  EXPECT_EQ(Count("SELECT running_run_id FROM job_state"),
            static_cast<int>(second));

  ASSERT_TRUE(yeet::store::FinishRun(db_, second, job.id, outcome, error))
      << error;
  EXPECT_EQ(Count("SELECT count(*) FROM job_state WHERE running_run_id IS "
                  "NULL"),
            1);
}

TEST_F(StoreTest, OrphansFromAPreviousBootAreReconciledAndCurrentOnesAreNot) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(yeet::db::Exec(
      db_,
      "INSERT INTO run (job_id, job_name, command, args, source, status, "
      "scheduled_at, started_at, boot_id) VALUES "
      "(1,'a','/bin/true','[]','cron','running','2026-09-26 "
      "11:00:00','2026-09-26 11:00:00','old'),"
      "(1,'a','/bin/true','[]','cron','running','2026-09-26 "
      "12:00:00','2026-09-26 12:00:00','current')",
      error))
      << error;

  int count = 0;
  ASSERT_TRUE(yeet::store::ReconcileOrphans(db_, "current", count, error))
      << error;

  EXPECT_EQ(count, 1);
  EXPECT_EQ(Count("SELECT count(*) FROM run WHERE status = 'orphaned'"), 1);
  EXPECT_EQ(Count("SELECT count(*) FROM run WHERE status = 'running'"), 1);
}

TEST_F(StoreTest, DeletingAJobCascadesToItsHistoryAndState) {
  Add("a", "/bin/true", "[]", "0 * * * * *");

  std::string error;
  ASSERT_TRUE(yeet::db::Exec(
      db_,
      "INSERT INTO run (job_id, job_name, command, args, source, status, "
      "scheduled_at, started_at, boot_id) VALUES (1,'a','/bin/true','[]',"
      "'cron','success','2026-09-26 12:00:00','2026-09-26 12:00:00','b')",
      error))
      << error;

  ASSERT_TRUE(yeet::db::Exec(db_, "DELETE FROM job WHERE id = 1", error))
      << error;

  EXPECT_EQ(Count("SELECT count(*) FROM run"), 0);
  EXPECT_EQ(Count("SELECT count(*) FROM job_state"), 0);
}

TEST_F(StoreTest, FindByNameReportsAMissingJobClearly) {
  JobRow job;
  std::string error;

  EXPECT_FALSE(yeet::store::FindByName(db_, "absent", job, error));
  EXPECT_NE(error.find("absent"), std::string::npos);
}

}
