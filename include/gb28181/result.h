#ifndef GB28181_INCLUDE_GB28181_RESULT_H_
#define GB28181_INCLUDE_GB28181_RESULT_H_
#include <optional>
#include <utility>
#include <variant>

#include "gb28181/error.h"

namespace gb28181 {
template <typename T>
class Result {
 public:
  Result(T value) : storage_(std::move(value)) {
  }

  Result(Error error) : storage_(std::move(error)) {
  }

  bool ok() const {
    return std::holds_alternative<T>(storage_);
  }

  explicit operator bool() const {
    return ok();
  }

  T& value() {
    return std::get<T>(storage_);
  }

  const T& value() const {
    return std::get<T>(storage_);
  }

  const Error& error() const {
    return std::get<Error>(storage_);
  }

 private:
  std::variant<T, Error> storage_;
};

template <>
class Result<void> {
 public:
  Result() = default;

  Result(Error error) : error_(std::move(error)) {
  }

  bool ok() const {
    return !error_.has_value();
  }

  explicit operator bool() const {
    return ok();
  }

  const Error& error() const {
    return error_.value();
  }

 private:
  std::optional<Error> error_;
};
}  // namespace gb28181
#endif  // GB28181_INCLUDE_GB28181_RESULT_H_
