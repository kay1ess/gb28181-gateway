#ifndef GB28181_TESTS_TEST_SUPPORT_H_
#define GB28181_TESTS_TEST_SUPPORT_H_
#include <cstdlib>
#include <iostream>
#include <string>

namespace gb28181::test {
inline void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

template <typename T>
void RequireOk(const T& result, const std::string& message) {
  if (!result.ok()) {
    Require(false, message + ": " + result.error().message);
  }
}
}  // namespace gb28181::test
#endif  // GB28181_TESTS_TEST_SUPPORT_H_
