#include "store.hpp"

#include <format>

#include "yeet/timefmt.hpp"

namespace yeet::store {

namespace {

class Stmt {
public:
  Stmt(sqlite3 *db, const char *sql) : db_(db) {
    ok_ = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) == SQLITE_OK;
  }

  Stmt(const Stmt &) = delete;
  Stmt &operator=(const Stmt &) = delete;

  ~Stmt() { sqlite3_finalize(stmt_); }

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] sqlite3_stmt *get() const noexcept { return stmt_; }

  void Text(int index, std::string_view value) {
    sqlite3_bind_text(stmt_, index, value.data(),
                      static_cast<int>(value.size()), SQLITE_TRANSIENT);
  }

  void OptionalText(int index, std::string_view value) {
    if (value.empty())
      sqlite3_bind_null(stmt_, index);
    else
      Text(index, value);
  }

  void Int(int index, int value) { sqlite3_bind_int(stmt_, index, value); }

  void Int64(int index, std::int64_t value) {
    sqlite3_bind_int64(stmt_, index, value);
  }

  void Time(int index, std::optional<schedule::TimePoint> value) {
    if (value.has_value())
      Text(index, timefmt::FormatUtc(*value));
    else
      sqlite3_bind_null(stmt_, index);
  }

  [[nodiscard]] std::string Error(std::string_view action) const {
    return std::format("{}: {}", action, sqlite3_errmsg(db_));
  }

private:
  sqlite3 *db_;
  sqlite3_stmt *stmt_ = nullptr;
  bool ok_ = false;
};

[[nodiscard]] std::string ColumnText(sqlite3_stmt *stmt, int index) {
  const auto *raw = sqlite3_column_text(stmt, index);
  if (raw == nullptr)
    return {};

  return std::string(
      reinterpret_cast<const char *>(raw),
      static_cast<std::size_t>(sqlite3_column_bytes(stmt, index)));
}

[[nodiscard]] bool RunToCompletion(Stmt &stmt, std::string_view action,
                                   std::string &error) {
  if (!stmt.ok()) {
    error = stmt.Error(action);
    return false;
  }

  if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
    error = stmt.Error(action);
    return false;
  }

  return true;
}

// SQLite parses the JSON array, so no JSON library is linked on either side
// and the CLI's stored form is the single representation.
[[nodiscard]] bool LoadArgs(sqlite3 *db, std::string_view json,
                            std::vector<std::string> &args,
                            std::string &error) {
  Stmt stmt(db, "SELECT value FROM json_each(?) ORDER BY key");
  if (!stmt.ok()) {
    error = stmt.Error("reading job arguments");
    return false;
  }

  stmt.Text(1, json);

  for (;;) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      args.push_back(ColumnText(stmt.get(), 0));
      continue;
    }

    if (rc == SQLITE_DONE)
      return true;

    error = stmt.Error("reading job arguments");
    return false;
  }
}

[[nodiscard]] bool ReadJob(sqlite3 *db, sqlite3_stmt *stmt, JobRow &job,
                           std::string &error) {
  job.id = sqlite3_column_int64(stmt, 0);
  job.name = ColumnText(stmt, 1);
  job.command = ColumnText(stmt, 2);
  job.cron = ColumnText(stmt, 4);
  job.timeout_s = sqlite3_column_int(stmt, 5);
  job.cwd = ColumnText(stmt, 6);
  job.parallel = ColumnText(stmt, 7) == "parallel";

  const std::string next = ColumnText(stmt, 8);
  job.next_fire_at = next.empty() ? std::nullopt : timefmt::ParseUtc(next);
  job.running = sqlite3_column_type(stmt, 9) != SQLITE_NULL;

  job.args.clear();

  return LoadArgs(db, ColumnText(stmt, 3), job.args, error);
}

constexpr const char *SelectJob =
    "SELECT j.id, j.name, j.command, j.args, COALESCE(j.cron, ''), "
    "j.timeout_s, COALESCE(j.cwd, ''), j.overlap_policy, "
    "COALESCE(s.next_fire_at, ''), s.running_run_id "
    "FROM job j JOIN job_state s ON s.job_id = j.id ";

} // namespace

[[nodiscard]] bool LoadSchedulable(sqlite3 *db, std::vector<JobRow> &jobs,
                                   std::string &error) {
  Stmt stmt(db, (std::string(SelectJob) +
                 "WHERE j.enabled = 1 AND j.cron IS NOT NULL ORDER BY j.id")
                    .c_str());

  if (!stmt.ok()) {
    error = stmt.Error("loading jobs");
    return false;
  }

  jobs.clear();

  for (;;) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
      JobRow job;
      if (!ReadJob(db, stmt.get(), job, error))
        return false;

      jobs.push_back(std::move(job));
      continue;
    }

    if (rc == SQLITE_DONE)
      return true;

    error = stmt.Error("loading jobs");
    return false;
  }
}

[[nodiscard]] bool FindByName(sqlite3 *db, std::string_view name, JobRow &job,
                              std::string &error) {
  Stmt stmt(db, (std::string(SelectJob) + "WHERE j.name = ?").c_str());
  if (!stmt.ok()) {
    error = stmt.Error("looking up job");
    return false;
  }

  stmt.Text(1, name);

  const int rc = sqlite3_step(stmt.get());
  if (rc == SQLITE_ROW)
    return ReadJob(db, stmt.get(), job, error);

  if (rc == SQLITE_DONE) {
    error = std::format("no job named '{}'", name);
    return false;
  }

  error = stmt.Error("looking up job");

  return false;
}

[[nodiscard]] bool SetNextFire(sqlite3 *db, std::int64_t job_id,
                               std::optional<schedule::TimePoint> next,
                               std::string &error) {
  Stmt stmt(db, "UPDATE job_state SET next_fire_at = ?, updated_at = "
                "CURRENT_TIMESTAMP WHERE job_id = ?");
  stmt.Time(1, next);
  stmt.Int64(2, job_id);

  return RunToCompletion(stmt, "updating next fire time", error);
}

[[nodiscard]] bool SetJobError(sqlite3 *db, std::int64_t job_id,
                               std::string_view message, std::string &error) {
  Stmt stmt(db, "UPDATE job_state SET last_error = ?, next_fire_at = NULL, "
                "updated_at = CURRENT_TIMESTAMP WHERE job_id = ?");
  stmt.OptionalText(1, message);
  stmt.Int64(2, job_id);

  return RunToCompletion(stmt, "recording job error", error);
}

[[nodiscard]] bool ClaimSlot(sqlite3 *db, const JobRow &job,
                             schedule::TimePoint slot,
                             std::optional<schedule::TimePoint> next,
                             std::string_view source, std::string_view boot_id,
                             std::int64_t &run_id, bool &claimed,
                             std::string &error) {
  claimed = false;

  if (!db::Exec(db, "BEGIN IMMEDIATE", error))
    return false;

  const auto rollback = [&](const std::string &message) {
    std::string ignored;
    (void)db::Exec(db, "ROLLBACK", ignored);
    error = message;
    return false;
  };

  {
    Stmt insert(db, "INSERT INTO run (job_id, job_name, command, args, source, "
                    "status, scheduled_at, started_at, boot_id) "
                    "SELECT ?, ?, ?, j.args, ?, 'running', ?, "
                    "strftime('%Y-%m-%d %H:%M:%S', 'now'), ? FROM job j WHERE "
                    "j.id = ?");
    insert.Int64(1, job.id);
    insert.Text(2, job.name);
    insert.Text(3, job.command);
    insert.Text(4, source);
    insert.Text(5, timefmt::FormatUtc(slot));
    insert.Text(6, boot_id);
    insert.Int64(7, job.id);

    std::string message;
    if (!RunToCompletion(insert, "recording run", message))
      return rollback(message);
  }

  run_id = sqlite3_last_insert_rowid(db);

  {
    // The predicate is the compare-and-swap: if anything else moved the slot,
    // no row changes and this claim is abandoned.
    Stmt advance(db, "UPDATE job_state SET next_fire_at = ?, last_fire_at = "
                     "?, running_run_id = ?, updated_at = CURRENT_TIMESTAMP "
                     "WHERE job_id = ? AND next_fire_at IS ?");
    advance.Time(1, next);
    advance.Text(2, timefmt::FormatUtc(slot));
    advance.Int64(3, run_id);
    advance.Int64(4, job.id);
    advance.Time(5, job.next_fire_at);

    std::string message;
    if (!RunToCompletion(advance, "advancing schedule", message))
      return rollback(message);
  }

  if (sqlite3_changes(db) == 0) {
    std::string ignored;
    (void)db::Exec(db, "ROLLBACK", ignored);
    return true;
  }

  if (!db::Exec(db, "COMMIT", error))
    return false;

  claimed = true;

  return true;
}

[[nodiscard]] bool ClaimManual(sqlite3 *db, const JobRow &job,
                               schedule::TimePoint at, std::string_view boot_id,
                               std::int64_t &run_id, bool &claimed,
                               std::string &error) {
  claimed = false;

  if (!db::Exec(db, "BEGIN IMMEDIATE", error))
    return false;

  const auto rollback = [&](const std::string &message) {
    std::string ignored;
    (void)db::Exec(db, "ROLLBACK", ignored);
    error = message;
    return false;
  };

  {
    Stmt insert(db, "INSERT INTO run (job_id, job_name, command, args, source, "
                    "status, scheduled_at, started_at, boot_id) "
                    "SELECT ?, ?, ?, j.args, 'manual', 'running', ?, "
                    "strftime('%Y-%m-%d %H:%M:%S', 'now'), ? FROM job j WHERE "
                    "j.id = ?");
    insert.Int64(1, job.id);
    insert.Text(2, job.name);
    insert.Text(3, job.command);
    insert.Text(4, timefmt::FormatUtc(at));
    insert.Text(5, boot_id);
    insert.Int64(6, job.id);

    std::string message;
    if (!RunToCompletion(insert, "recording run", message))
      return rollback(message);
  }

  run_id = sqlite3_last_insert_rowid(db);

  {
    // Read under the write lock, not from the JobRow, so a run that started
    // after the lookup is still seen.
    Stmt take(db, "UPDATE job_state SET running_run_id = ?, updated_at = "
                  "CURRENT_TIMESTAMP WHERE job_id = ? AND (running_run_id IS "
                  "NULL OR (SELECT overlap_policy FROM job WHERE id = ?) = "
                  "'parallel')");
    take.Int64(1, run_id);
    take.Int64(2, job.id);
    take.Int64(3, job.id);

    std::string message;
    if (!RunToCompletion(take, "marking job running", message))
      return rollback(message);
  }

  if (sqlite3_changes(db) == 0) {
    std::string ignored;
    (void)db::Exec(db, "ROLLBACK", ignored);
    run_id = 0;
    return true;
  }

  if (!db::Exec(db, "COMMIT", error))
    return false;

  claimed = true;

  return true;
}

[[nodiscard]] bool RecordMisfire(sqlite3 *db, const JobRow &job,
                                 schedule::TimePoint slot, int count,
                                 std::string_view status,
                                 std::string_view boot_id, std::string &error) {
  Stmt stmt(db, "INSERT INTO run (job_id, job_name, command, args, source, "
                "status, scheduled_at, started_at, finished_at, "
                "misfire_count, boot_id) "
                "SELECT ?, ?, ?, j.args, 'cron', ?, ?, "
                "strftime('%Y-%m-%d %H:%M:%S', 'now'), "
                "strftime('%Y-%m-%d %H:%M:%S', 'now'), ?, ? "
                "FROM job j WHERE j.id = ?");
  stmt.Int64(1, job.id);
  stmt.Text(2, job.name);
  stmt.Text(3, job.command);
  stmt.Text(4, status);
  stmt.Text(5, timefmt::FormatUtc(slot));
  stmt.Int(6, count);
  stmt.Text(7, boot_id);
  stmt.Int64(8, job.id);

  return RunToCompletion(stmt, "recording misfire", error);
}

[[nodiscard]] bool StartRun(sqlite3 *db, std::int64_t run_id, int pid,
                            std::string &error) {
  Stmt stmt(db, "UPDATE run SET pid = ? WHERE id = ?");
  stmt.Int(1, pid);
  stmt.Int64(2, run_id);

  return RunToCompletion(stmt, "recording pid", error);
}

[[nodiscard]] bool FinishRun(sqlite3 *db, std::int64_t run_id,
                             std::int64_t job_id, const exec::Outcome &outcome,
                             std::string &error) {
  const char *status = "failure";
  if (outcome.Succeeded())
    status = "success";
  else if (outcome.timed_out)
    status = "timeout";

  {
    Stmt stmt(db, "UPDATE run SET status = ?, finished_at = "
                  "strftime('%Y-%m-%d %H:%M:%S', 'now'), duration_ms = ?, "
                  "exit_code = ?, term_signal = ?, stdout_text = ?, "
                  "stderr_text = ?, stdout_bytes = ?, stderr_bytes = ?, "
                  "truncated = ?, error = ? WHERE id = ?");
    stmt.Text(1, status);
    stmt.Int64(2, outcome.duration.count());
    if (outcome.exited)
      stmt.Int(3, outcome.exit_code);
    else
      sqlite3_bind_null(stmt.get(), 3);
    stmt.Int(4, outcome.term_signal);
    stmt.OptionalText(5, outcome.out);
    stmt.OptionalText(6, outcome.err);
    stmt.Int64(7, static_cast<std::int64_t>(outcome.out_bytes));
    stmt.Int64(8, static_cast<std::int64_t>(outcome.err_bytes));
    stmt.Int(9, outcome.truncated ? 1 : 0);
    stmt.OptionalText(10, outcome.error);
    stmt.Int64(11, run_id);

    if (!RunToCompletion(stmt, "finishing run", error))
      return false;
  }

  // Only the run holding running_run_id releases it, so a parallel run
  // finishing first cannot clear the marker of one still in flight.
  Stmt state(db, "UPDATE job_state SET running_run_id = "
                 "NULLIF(running_run_id, ?), "
                 "consecutive_failures = CASE WHEN ? THEN 0 ELSE "
                 "consecutive_failures + 1 END, last_error = ?, updated_at = "
                 "CURRENT_TIMESTAMP WHERE job_id = ?");
  state.Int64(1, run_id);
  state.Int(2, outcome.Succeeded() ? 1 : 0);
  state.OptionalText(3, outcome.Succeeded() ? std::string_view{}
                                            : std::string_view{outcome.error});
  state.Int64(4, job_id);

  return RunToCompletion(state, "updating job state", error);
}

[[nodiscard]] bool FailRun(sqlite3 *db, std::int64_t run_id,
                           std::int64_t job_id, std::string_view message,
                           std::string &error) {
  {
    Stmt stmt(db, "UPDATE run SET status = 'spawn_error', finished_at = "
                  "strftime('%Y-%m-%d %H:%M:%S', 'now'), error = ? WHERE id = "
                  "?");
    stmt.Text(1, message);
    stmt.Int64(2, run_id);

    if (!RunToCompletion(stmt, "recording spawn failure", error))
      return false;
  }

  Stmt state(db, "UPDATE job_state SET running_run_id = "
                 "NULLIF(running_run_id, ?), "
                 "consecutive_failures = consecutive_failures + 1, "
                 "last_error = ?, updated_at = CURRENT_TIMESTAMP WHERE job_id "
                 "= ?");
  state.Int64(1, run_id);
  state.Text(2, message);
  state.Int64(3, job_id);

  return RunToCompletion(state, "updating job state", error);
}

[[nodiscard]] bool ReconcileOrphans(sqlite3 *db, std::string_view boot_id,
                                    int &count, std::string &error) {
  {
    Stmt stmt(db, "UPDATE run SET status = 'orphaned', finished_at = "
                  "strftime('%Y-%m-%d %H:%M:%S', 'now'), error = 'daemon "
                  "exited while the run was in flight' WHERE status = "
                  "'running' AND boot_id <> ?");
    stmt.Text(1, boot_id);

    if (!RunToCompletion(stmt, "reconciling orphaned runs", error))
      return false;
  }

  count = sqlite3_changes(db);

  return db::Exec(db,
                  "UPDATE job_state SET running_run_id = NULL WHERE "
                  "running_run_id IS NOT NULL",
                  error);
}

} // namespace yeet::store
