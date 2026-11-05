#include "src/core/task_runner.h"

#include <future>
#include <utility>

namespace gb28181 {
TaskRunner::TaskRunner() : thread_([this] { Run(); }) {
}

TaskRunner::~TaskRunner() {
  Stop();
}

bool TaskRunner::Post(std::function<void()> task) {
  return PostAfter(std::chrono::milliseconds(0), std::move(task));
}

bool TaskRunner::PostAfter(std::chrono::milliseconds delay,
                           std::function<void()> task) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_)
    return false;
  tasks_.emplace(std::chrono::steady_clock::now() + delay, std::move(task));
  wake_.notify_one();
  return true;
}

bool TaskRunner::IsCurrent() const {
  return thread_.get_id() == std::this_thread::get_id();
}

void TaskRunner::Drain() {
  auto barrier = std::make_shared<std::promise<void>>();
  auto completed = barrier->get_future();
  if (Post([barrier] { barrier->set_value(); }))
    completed.wait();
}

void TaskRunner::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    tasks_.clear();
  }
  wake_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void TaskRunner::Run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stopping_) {
    if (tasks_.empty()) {
      wake_.wait(lock);
      continue;
    }
    const auto deadline = tasks_.begin()->first;
    if (std::chrono::steady_clock::now() < deadline) {
      wake_.wait_until(lock, deadline);
      continue;
    }
    auto task = std::move(tasks_.begin()->second);
    tasks_.erase(tasks_.begin());
    lock.unlock();
    try {
      task();
    } catch (...) { /* User callbacks cannot kill the worker. */
    }
    lock.lock();
  }
}
}  // namespace gb28181
