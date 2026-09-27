#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "yeet/db.hpp"
#include "yeet/exec/collect.hpp"
#include "yeet/schedule/spec.hpp"

namespace yeet::store {

struct JobRow {
  std::int64_t id = 0;
  std::string name;
  std::string command;
  std::vector<std::string> args;
  std::string cron;
  int timeout_s = 300;
  std::string cwd;
  bool parallel = false;
  std::optional<schedule::TimePoint> next_fire_at;
  bool running = false;
};

[[nodiscard]] bool LoadSchedulable(sqlite3 *db, std::vector<JobRow> &jobs,
                                   std::string &error);

[[nodiscard]] bool FindByName(sqlite3 *db, std::string_view name, JobRow &job,
                              std::string &error);

[[nodiscard]] bool SetNextFire(sqlite3 *db, std::int64_t job_id,
                               std::optional<schedule::TimePoint> next,
                               std::string &error);

[[nodiscard]] bool SetJobError(sqlite3 *db, std::int64_t job_id,
                               std::string_view message, std::string &error);

[[nodiscard]] bool ClaimSlot(sqlite3 *db, const JobRow &job,
                             schedule::TimePoint slot,
                             std::optional<schedule::TimePoint> next,
                             std::string_view source, std::string_view boot_id,
                             std::int64_t &run_id, bool &claimed,
                             std::string &error);

[[nodiscard]] bool ClaimManual(sqlite3 *db, const JobRow &job,
                               schedule::TimePoint at, std::string_view boot_id,
                               std::int64_t &run_id, bool &claimed,
                               std::string &error);

[[nodiscard]] bool RecordMisfire(sqlite3 *db, const JobRow &job,
                                 schedule::TimePoint slot, int count,
                                 std::string_view status,
                                 std::string_view boot_id, std::string &error);

[[nodiscard]] bool StartRun(sqlite3 *db, std::int64_t run_id, int pid,
                            std::string &error);

[[nodiscard]] bool FinishRun(sqlite3 *db, std::int64_t run_id,
                             std::int64_t job_id, const exec::Outcome &outcome,
                             std::string &error);

[[nodiscard]] bool FailRun(sqlite3 *db, std::int64_t run_id,
                           std::int64_t job_id, std::string_view message,
                           std::string &error);

[[nodiscard]] bool ReconcileOrphans(sqlite3 *db, std::string_view boot_id,
                                    int &count, std::string &error);

}
