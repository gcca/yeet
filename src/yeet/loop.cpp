#include "loop.hpp"

#include <atomic>
#include <csignal>
#include <format>
#include <map>
#include <random>
#include <thread>

#include "yeet/exec/collect.hpp"
#include "yeet/exec/spawn.hpp"
#include "yeet/log.hpp"
#include "yeet/schedule/plan.hpp"
#include "yeet/timefmt.hpp"

namespace yeet::loop {

namespace {

std::atomic<bool> stop_requested{false};

void RequestStop(int) { stop_requested.store(true, std::memory_order_relaxed); }

void InstallSignalHandlers() {
  struct sigaction action{};
  action.sa_handler = RequestStop;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);

  // Writing to a pipe whose reader died must not kill the daemon.
  struct sigaction ignore{};
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  sigaction(SIGPIPE, &ignore, nullptr);
}

[[nodiscard]] schedule::TimePoint Now() {
  return std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
}

[[nodiscard]] exec::CollectOptions CollectFor(const conf::Settings &settings,
                                              const store::JobRow &job) {
  exec::CollectOptions options;
  options.timeout = std::chrono::seconds(
      job.timeout_s > 0 ? job.timeout_s : settings.YEET_JOB_TIMEOUT.count());
  options.term_grace = settings.YEET_TERM_GRACE;
  options.output_cap = settings.YEET_OUTPUT_CAP;

  return options;
}

void LogOutcome(const store::JobRow &job, const exec::Outcome &outcome,
                std::int64_t run_id) {
  if (outcome.Succeeded()) {
    log::Info("ran job={} run={} duration={}ms", job.name, run_id,
              outcome.duration.count());
    return;
  }

  if (outcome.timed_out) {
    log::Error("timeout job={} run={} after {}ms", job.name, run_id,
               outcome.duration.count());
    return;
  }

  log::Error("failed job={} run={} exit={} signal={}", job.name, run_id,
             outcome.exit_code, outcome.term_signal);
}

// Runs one already-claimed slot to completion.
void Execute(sqlite3 *db, const conf::Settings &settings,
             const store::JobRow &job, std::int64_t run_id) {
  exec::SpawnRequest request;
  request.command = job.command;
  request.args = job.args;
  if (!job.cwd.empty())
    request.cwd = job.cwd;

  exec::Spawned child;
  std::string error;

  if (!exec::Spawn(request, child, error)) {
    log::Error("spawn error job={}: {}", job.name, error);

    std::string ignored;
    (void)store::FailRun(db, run_id, job.id, error, ignored);
    return;
  }

  std::string ignored;
  (void)store::StartRun(db, run_id, static_cast<int>(child.pid), ignored);

  const exec::Outcome outcome = exec::Collect(child, CollectFor(settings, job));

  LogOutcome(job, outcome, run_id);

  if (!outcome.err.empty())
    log::Debug("stderr job={} run={}: {}", job.name, run_id, outcome.err);

  if (!store::FinishRun(db, run_id, job.id, outcome, error))
    log::Error("could not record run={}: {}", run_id, error);
}

} // namespace

[[nodiscard]] std::string NewBootId() {
  std::random_device source;
  std::uniform_int_distribution<std::uint32_t> digits;

  return std::format("{:08x}{:08x}", digits(source), digits(source));
}

[[nodiscard]] TickResult Tick(sqlite3 *db, const conf::Settings &settings,
                              std::string_view boot_id,
                              schedule::TimePoint now) {
  TickResult result;

  std::vector<store::JobRow> jobs;
  std::string error;

  if (!store::LoadSchedulable(db, jobs, error)) {
    log::Error("{}", error);
    result.failed = true;
    return result;
  }

  const auto note_earliest = [&result](std::optional<schedule::TimePoint> at) {
    if (!at.has_value())
      return;

    if (!result.earliest.has_value() || *at < *result.earliest)
      result.earliest = at;
  };

  for (const store::JobRow &job : jobs) {
    schedule::Spec spec;
    if (!schedule::Parse(job.cron, spec, error)) {
      log::Error("invalid cron job={} '{}': {}", job.name, job.cron, error);

      std::string ignored;
      (void)store::SetJobError(db, job.id, error, ignored);
      continue;
    }

    schedule::PlanInput input;
    input.spec = &spec;
    input.next_fire_at = job.next_fire_at;
    input.running = job.running && !job.parallel;
    input.catchup = schedule::Catchup::Once;

    const schedule::Plan plan = schedule::MakePlan(input, now);

    if (plan.misfired > 0 && plan.first_misfire.has_value()) {
      const char *status = plan.skipped_overlap ? "skipped" : "misfire";

      std::string ignored;
      if (!store::RecordMisfire(db, job, *plan.first_misfire, plan.misfired,
                                status, boot_id, ignored))
        log::Warn("could not record {} for job={}: {}", status, job.name,
                  ignored);
      else
        log::Warn("{} job={} slots={} from={}", status, job.name, plan.misfired,
                  timefmt::FormatUtc(*plan.first_misfire));
    }

    note_earliest(plan.next_fire_at);

    if (plan.fire.empty()) {
      if (plan.next_fire_at != job.next_fire_at &&
          !store::SetNextFire(db, job.id, plan.next_fire_at, error))
        log::Error("could not update job={}: {}", job.name, error);

      continue;
    }

    for (const schedule::TimePoint slot : plan.fire) {
      const bool is_catchup = slot < now;

      std::int64_t run_id = 0;
      bool claimed = false;

      if (!store::ClaimSlot(db, job, slot, plan.next_fire_at,
                            is_catchup ? "catchup" : "cron", boot_id, run_id,
                            claimed, error)) {
        log::Error("could not claim slot for job={}: {}", job.name, error);
        break;
      }

      if (!claimed) {
        log::Debug("slot for job={} was taken by another writer", job.name);
        break;
      }

      Execute(db, settings, job, run_id);
      ++result.ran;
    }
  }

  return result;
}

[[nodiscard]] int TriggerOne(sqlite3 *db, const conf::Settings &settings,
                             std::string_view boot_id, std::string_view name) {
  store::JobRow job;
  std::string error;

  if (!store::FindByName(db, name, job, error)) {
    log::Error("{}", error);
    return 1;
  }

  exec::SpawnRequest request;
  request.command = job.command;
  request.args = job.args;
  if (!job.cwd.empty())
    request.cwd = job.cwd;

  const schedule::TimePoint now = Now();

  std::int64_t run_id = 0;
  bool claimed = false;

  if (!store::ClaimManual(db, job, now, boot_id, run_id, claimed, error)) {
    log::Error("{}", error);
    return 1;
  }

  if (!claimed) {
    log::Error("job={} is already running and its overlap policy is skip",
               job.name);
    return 1;
  }

  exec::Spawned child;
  if (!exec::Spawn(request, child, error)) {
    log::Error("spawn error job={}: {}", job.name, error);

    std::string ignored;
    (void)store::FailRun(db, run_id, job.id, error, ignored);
    return 1;
  }

  std::string ignored;
  (void)store::StartRun(db, run_id, static_cast<int>(child.pid), ignored);

  const exec::Outcome outcome = exec::Collect(child, CollectFor(settings, job));

  // trigger relays the child's streams so it behaves like running the program
  // directly; the daemon path only logs.
  if (!outcome.out.empty())
    std::fwrite(outcome.out.data(), 1, outcome.out.size(), stdout);
  if (!outcome.err.empty())
    std::fwrite(outcome.err.data(), 1, outcome.err.size(), stderr);

  if (!store::FinishRun(db, run_id, job.id, outcome, error))
    log::Error("could not record run={}: {}", run_id, error);

  // 3 distinguishes "the job failed" from "yeet failed".
  return outcome.Succeeded() ? 0 : 3;
}

[[nodiscard]] int Serve(sqlite3 *db, const conf::Settings &settings,
                        const Options &options) {
  InstallSignalHandlers();

  const std::string boot_id = NewBootId();

  std::string error;
  int orphaned = 0;
  if (!store::ReconcileOrphans(db, boot_id, orphaned, error)) {
    log::Error("{}", error);
    return 1;
  }

  if (orphaned > 0)
    log::Warn("marked {} run(s) orphaned from a previous boot", orphaned);

  log::Info("starting boot={} db={}", boot_id, settings.YEET_DB);

  for (;;) {
    if (stop_requested.load(std::memory_order_relaxed)) {
      log::Info("stopping on signal");
      break;
    }

    const schedule::TimePoint now = Now();
    const TickResult tick = Tick(db, settings, boot_id, now);

    if (tick.failed)
      return 1;

    if (options.once)
      break;

    // Sleep until the earliest scheduled instant rather than on a fixed
    // period, clamped so a clock jump cannot park the loop and so a CLI edit
    // is picked up within one clamp interval.
    const auto wait =
        schedule::SleepFor(tick.earliest, Now(), settings.YEET_MAX_SLEEP);

    // Short steps so a signal is noticed promptly without a self-pipe.
    const auto deadline = std::chrono::steady_clock::now() + wait;

    while (std::chrono::steady_clock::now() < deadline &&
           !stop_requested.load(std::memory_order_relaxed))
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}

} // namespace yeet::loop
