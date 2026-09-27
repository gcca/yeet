#include "spec.hpp"

#include <cctype>
#include <format>
#include <vector>

namespace yeet::schedule {

namespace {

[[nodiscard]] std::vector<std::string> Fields(std::string_view text) {
  std::vector<std::string> fields;
  std::size_t index = 0;

  while (index < text.size()) {
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index])) != 0)
      ++index;

    const std::size_t start = index;
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index])) == 0)
      ++index;

    if (index > start)
      fields.emplace_back(text.substr(start, index - start));
  }

  return fields;
}

[[nodiscard]] bool ExpandMacro(std::string_view text, std::string &expanded) {
  if (text == "@yearly" || text == "@annually")
    expanded = "0 0 0 1 1 *";
  else if (text == "@monthly")
    expanded = "0 0 0 1 * *";
  else if (text == "@weekly")
    expanded = "0 0 0 * * 0";
  else if (text == "@daily" || text == "@midnight")
    expanded = "0 0 0 * * *";
  else if (text == "@hourly")
    expanded = "0 0 * * * *";
  else
    return false;

  return true;
}

}

[[nodiscard]] bool Normalize(std::string_view text, std::string &normalized,
                             std::string &error) {
  const std::vector<std::string> raw = Fields(text);

  if (raw.empty()) {
    error = "cron expression is empty";
    return false;
  }

  if (raw.size() == 1 && raw.front().starts_with('@')) {
    if (ExpandMacro(raw.front(), normalized))
      return true;

    error = std::format("unsupported macro '{}'", raw.front());
    return false;
  }

  if (raw.size() == 5) {
    normalized =
        std::format("0 {} {} {} {} {}", raw[0], raw[1], raw[2], raw[3], raw[4]);
    return true;
  }

  if (raw.size() == 6 || raw.size() == 7) {
    normalized.clear();
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (i > 0)
        normalized.push_back(' ');
      normalized.append(raw[i]);
    }
    return true;
  }

  error =
      std::format("cron expression needs 5, 6 or 7 fields, got {}", raw.size());
  return false;
}

[[nodiscard]] bool Parse(std::string_view text, Spec &spec,
                         std::string &error) {
  std::string normalized;
  if (!Normalize(text, normalized, error))
    return false;

  try {
    spec.expr = ::cron::make_cron(normalized);
    spec.normalized = std::move(normalized);
    return true;
  } catch (const ::cron::bad_cronexpr &failure) {
    error = failure.what();
    return false;
  }
}

[[nodiscard]] std::optional<TimePoint> NextAfter(const Spec &spec,
                                                 TimePoint from) {
  try {
    const TimePoint next = ::cron::cron_next(spec.expr, from);
    if (next == TimePoint::min())
      return std::nullopt;

    return next;
  } catch (const ::cron::bad_cronexpr &) {
    return std::nullopt;
  }
}

}
