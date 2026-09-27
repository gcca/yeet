#pragma once

#include <string>

#include <sqlite3.h>

namespace yeet::db {

inline constexpr int SchemaVersion = 1;

[[nodiscard]] sqlite3 *Connect(const std::string &path, std::string &error);

// The schema belongs to yeet-initdb. The daemon only checks that the version
// it was built against is the one present, and refuses to run otherwise
// rather than idling against a table it cannot use.
[[nodiscard]] bool RequireSchema(sqlite3 *db, std::string &error);

[[nodiscard]] bool Exec(sqlite3 *db, const char *sql, std::string &error);

} // namespace yeet::db
