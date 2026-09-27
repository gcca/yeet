#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace yeet::exec {

// Keeps the first and last bytes of a stream within a fixed budget. Head and
// tail both matter: the head carries the banner a program prints, the tail
// carries the error it died on.
//
// Appending past the cap is not an error. The reader must keep draining a
// child's pipe after the budget is spent, or the pipe fills and the child
// blocks forever, so this turns a memory bound into a discard rather than a
// stop signal.
class Capture {
public:
  explicit Capture(std::size_t cap);

  void Append(std::string_view chunk);

  [[nodiscard]] std::string Text() const;
  [[nodiscard]] std::uint64_t TotalBytes() const noexcept;
  [[nodiscard]] bool Truncated() const noexcept;

private:
  std::size_t cap_;
  std::size_t head_cap_;
  std::size_t tail_cap_;
  std::string head_;
  std::string tail_;
  std::uint64_t total_ = 0;
};

} // namespace yeet::exec
