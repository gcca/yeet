#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "yeet/exec/spawn.hpp"

namespace yeet::exec {

struct CollectOptions {
  std::chrono::seconds timeout{300};
  std::chrono::seconds term_grace{10};
  std::chrono::seconds kill_grace{5};
  std::size_t output_cap = 65536;
};

struct Outcome {
  bool timed_out = false;
  bool exited = false;
  bool unreaped = false;
  int exit_code = -1;
  int term_signal = 0;
  std::string out;
  std::string err;
  std::string error;
  std::uint64_t out_bytes = 0;
  std::uint64_t err_bytes = 0;
  bool truncated = false;
  std::chrono::milliseconds duration{0};

  [[nodiscard]] bool Succeeded() const noexcept {
    return !timed_out && exited && exit_code == 0;
  }
};

// Drains both pipes to EOF, then reaps. If the child is still alive at the
// timeout, whether or not its pipes are open, its process group gets SIGTERM,
// then SIGKILL after the grace period.
[[nodiscard]] Outcome Collect(Spawned &child, const CollectOptions &options);

} // namespace yeet::exec
