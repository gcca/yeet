#include "db.hpp"

#include <filesystem>
#include <format>

namespace yeet::db {

namespace {

[[nodiscard]] std::string Message(sqlite3 *db, std::string_view action) {
  return std::format("{}: {}", action, sqlite3_errmsg(db));
}

}

[[nodiscard]] bool Exec(sqlite3 *db, const char *sql, std::string &error) {
  char *raw = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &raw) == SQLITE_OK)
    return true;

  error = raw ? raw : "unknown sqlite error";
  sqlite3_free(raw);

  return false;
}

[[nodiscard]] sqlite3 *Connect(const std::string &path, std::string &error) {
  if (!std::filesystem::exists(path)) {
    error = std::format("database '{}' does not exist, run yeet-initdb", path);
    return nullptr;
  }

  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(path.c_str(), &db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    error = std::format("cannot open '{}': {}", path,
                        db ? sqlite3_errmsg(db) : "out of memory");
    sqlite3_close(db);
    return nullptr;
  }

  sqlite3_busy_timeout(db, 5000);

  if (!Exec(db, "PRAGMA foreign_keys = ON", error) ||
      !Exec(db, "PRAGMA journal_mode = WAL", error) ||
      !Exec(db, "PRAGMA synchronous = NORMAL", error)) {
    sqlite3_close(db);
    return nullptr;
  }

  return db;
}

[[nodiscard]] bool RequireSchema(sqlite3 *db, std::string &error) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    error = Message(db, "reading schema version");
    return false;
  }

  int version = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    version = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  if (version == SchemaVersion)
    return true;

  if (version <= 0) {
    error = "database is not initialized, run yeet-initdb";
    return false;
  }

  error = std::format(
      "database schema version is {} but this build requires {}, run "
      "yeet-initdb",
      version, SchemaVersion);

  return false;
}

}
