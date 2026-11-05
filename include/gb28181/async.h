#ifndef GB28181_INCLUDE_GB28181_ASYNC_H_
#define GB28181_INCLUDE_GB28181_ASYNC_H_
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "gb28181/result.h"

namespace gb28181 {
template <typename T>
using Completion = std::function<void(Result<T>)>;

class Cancellation {
 public:
  Cancellation() : flag_(std::make_shared<std::atomic<bool>>(false)) {
  }

  void Cancel() const {
    flag_->store(true);
  }

  bool cancelled() const {
    return flag_->load();
  }

 private:
  std::shared_ptr<std::atomic<bool>> flag_;
};

struct RequestOptions {
  std::chrono::milliseconds timeout{10000};
  Cancellation cancellation;
  std::string trace_id;
};
}  // namespace gb28181
#endif  // GB28181_INCLUDE_GB28181_ASYNC_H_
