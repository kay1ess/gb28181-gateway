#include "src/adapters/storage/sqlite/database.h"

#include <sqlite3.h>

namespace gb28181::storage {
DatabaseError::DatabaseError(int code, const char* message)
    : std::runtime_error(message), code_(code) {
}

Error TranslateError(const DatabaseError& error, bool writing) {
  const int primary = error.code() & 0xff;
  ErrorCode code =
      writing ? ErrorCode::kStorageWriteFailed : ErrorCode::kStorageReadFailed;
  if (primary == SQLITE_BUSY || primary == SQLITE_LOCKED)
    code = ErrorCode::kStorageBusy;
  if (error.code() == SQLITE_CONSTRAINT_UNIQUE ||
      error.code() == SQLITE_CONSTRAINT_PRIMARYKEY)
    code = ErrorCode::kAlreadyExists;
  return {code, ErrorMessage(code), {}};
}

Database::~Database() {
  Close();
}

void Database::Open(const std::string& path) {
  if (handle_)
    throw DatabaseError(SQLITE_MISUSE, "Database already open");
  const int code = sqlite3_open_v2(
      path.c_str(), &handle_,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
      nullptr);
  if (code != SQLITE_OK) {
    Close();
    throw DatabaseError(code, "Cannot open database");
  }
  sqlite3_extended_result_codes(handle_, 1);
  sqlite3_busy_timeout(handle_, 1000);
  try {
    Execute(
        "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA "
        "synchronous=NORMAL;");
  } catch (...) {
    Close();
    throw;
  }
}

void Database::Close() {
  if (handle_) {
    sqlite3_close_v2(handle_);
    handle_ = nullptr;
  }
}

void Database::Execute(const char* sql) {
  if (!handle_)
    throw DatabaseError(SQLITE_MISUSE, "Database not open");
  const int code = sqlite3_exec(handle_, sql, nullptr, nullptr, nullptr);
  if (code != SQLITE_OK)
    throw DatabaseError(sqlite3_extended_errcode(handle_),
                        sqlite3_errmsg(handle_));
}

int Database::Changes() const {
  return sqlite3_changes(handle_);
}

Statement::Statement(Database& database, const char* sql) {
  if (!database.handle())
    throw DatabaseError(SQLITE_MISUSE, "Database not open");
  const int code =
      sqlite3_prepare_v2(database.handle(), sql, -1, &handle_, nullptr);
  if (code != SQLITE_OK)
    throw DatabaseError(code, sqlite3_errmsg(database.handle()));
}

Statement::~Statement() {
  sqlite3_finalize(handle_);
}

void Statement::Check(int code) {
  if (code != SQLITE_OK)
    throw DatabaseError(code, sqlite3_errmsg(sqlite3_db_handle(handle_)));
}

void Statement::Bind(int index, const std::string& value) {
  Check(sqlite3_bind_text(handle_, index, value.data(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

void Statement::Bind(int index, int64_t value) {
  Check(sqlite3_bind_int64(handle_, index, value));
}

bool Statement::Step() {
  const int code = sqlite3_step(handle_);
  if (code == SQLITE_ROW)
    return true;
  if (code == SQLITE_DONE)
    return false;
  throw DatabaseError(code, sqlite3_errmsg(sqlite3_db_handle(handle_)));
}

void Statement::Reset() {
  Check(sqlite3_reset(handle_));
  Check(sqlite3_clear_bindings(handle_));
}

std::string Statement::Text(int column) const {
  const auto* text = sqlite3_column_text(handle_, column);
  return text ? std::string(reinterpret_cast<const char*>(text),
                            sqlite3_column_bytes(handle_, column))
              : std::string();
}

int64_t Statement::Integer(int column) const {
  return sqlite3_column_int64(handle_, column);
}

Transaction::Transaction(Database& database) : database_(database) {
  database_.Execute("BEGIN IMMEDIATE");
}

Transaction::~Transaction() {
  if (!committed_) {
    try {
      database_.Execute("ROLLBACK");
    } catch (...) {
    }
  }
}

void Transaction::Commit() {
  database_.Execute("COMMIT");
  committed_ = true;
}
}  // namespace gb28181::storage
