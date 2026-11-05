#ifndef GB28181_SRC_CORE_TASK_RUNNER_H_
#define GB28181_SRC_CORE_TASK_RUNNER_H_
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace gb28181 {
// Stop is owner-thread only; never call from a task on this runner. Pending
// tasks are discarded. The owner must settle public operations before Stop.
class TaskRunner {
 public:
  TaskRunner();
  ~TaskRunner();
  TaskRunner(const TaskRunner&) = delete;
  TaskRunner& operator=(const TaskRunner&) = delete;
  bool Post(std::function<void()> task);
  bool PostAfter(std::chrono::milliseconds delay, std::function<void()> task);
  // Owner-thread barrier for work already due; future timers are not awaited.
  void Drain();
  void Stop();
  bool IsCurrent() const;

 private:
  void Run();
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::multimap<std::chrono::steady_clock::time_point, std::function<void()>>
      tasks_;
  std::thread thread_;
};
}  // namespace gb28181
#endif  // GB28181_SRC_CORE_TASK_RUNNER_H_
