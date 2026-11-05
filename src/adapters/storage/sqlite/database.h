#ifndef GB28181_SRC_ADAPTERS_STORAGE_SQLITE_DATABASE_H_
#define GB28181_SRC_ADAPTERS_STORAGE_SQLITE_DATABASE_H_
#include <cstdint>
#include <stdexcept>
#include <string>

#include "gb28181/error.h"
struct sqlite3;
struct sqlite3_stmt;

namespace gb28181::storage {
// Exceptions are private to the adapter and converted at repository boundaries.
class DatabaseError : public std::runtime_error {
 public:
  DatabaseError(int code, const char* message);

  int code() const {
    return code_;
  }

 private:
  int code_;
};

Error TranslateError(const DatabaseError& error, bool writing);

class Database {
 public:
  Database() = default;
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  void Open(const std::string& path);
  void Close();
  void Execute(const char* sql);

  sqlite3* handle() const {
    return handle_;
  }

  int Changes() const;

 private:
  sqlite3* handle_ = nullptr;
};

class Statement {
 public:
  Statement(Database& database, const char* sql);
  ~Statement();
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  void Bind(int index, const std::string& value);
  void Bind(int index, int64_t value);
  bool Step();
  void Reset();
  std::string Text(int column) const;
  int64_t Integer(int column) const;

 private:
  void Check(int code);
  sqlite3_stmt* handle_ = nullptr;
};

class Transaction {
 public:
  explicit Transaction(Database& database);
  ~Transaction();
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  void Commit();

 private:
  Database& database_;
  bool committed_ = false;
};
}  // namespace gb28181::storage
#endif  // GB28181_SRC_ADAPTERS_STORAGE_SQLITE_DATABASE_H_
