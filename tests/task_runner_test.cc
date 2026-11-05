#include "src/core/task_runner.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

#include "gb28181/async.h"
#include "tests/test_support.h"

int main() {
  using namespace std::chrono_literals;
  gb28181::TaskRunner runner;
  std::promise<int> done;
  std::atomic<int> called{0};
  runner.Post([] { throw std::runtime_error("user callback"); });
  runner.PostAfter(20ms, [&] { done.set_value(++called); });
  auto future = done.get_future();
  gb28181::test::Require(future.wait_for(3s) == std::future_status::ready,
                         "throwing callback must not stop worker");
  gb28181::test::Require(future.get() == 1, "scheduled callback runs once");
  runner.Post([&] {
    gb28181::test::Require(runner.IsCurrent(), "task runs on owned executor");
    ++called;
  });
  runner.Drain();
  gb28181::test::Require(called == 2,
                         "drain waits for accepted ready callbacks");
  runner.PostAfter(1h, [&] { ++called; });
  const auto start = std::chrono::steady_clock::now();
  runner.Stop();
  gb28181::test::Require(std::chrono::steady_clock::now() - start < 1s,
                         "stop interrupts long timer");
  gb28181::test::Require(!runner.Post([] {}), "stopped runner rejects work");
  gb28181::test::Require(called == 2, "pending task discarded");
  gb28181::Cancellation cancellation;
  auto copy = cancellation;
  copy.Cancel();
  gb28181::test::Require(cancellation.cancelled(),
                         "cancellation token shared across copies");
  std::cout << "task runner tests passed\n";
}
