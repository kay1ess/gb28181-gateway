#ifndef GB28181_TESTS_SDK_FLOW_H_
#define GB28181_TESTS_SDK_FLOW_H_
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include "gb28181/client.h"

namespace gb28181::test {
inline void Require(bool condition, const std::string& message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename T, typename Invoke>
Result<T> Await(Invoke invoke) {
  auto promise = std::make_shared<std::promise<Result<T>>>();
  auto future = promise->get_future();
  auto accepted = invoke(
      [promise](Result<T> result) { promise->set_value(std::move(result)); });
  Require(accepted.ok(),
          accepted ? "" : "rejected: " + accepted.error().message);
  Require(
      future.wait_for(std::chrono::seconds(20)) == std::future_status::ready,
      "missing SDK completion");
  return future.get();
}

template <typename T>
T Unwrap(Result<T> result) {
  Require(result.ok(),
          result
              ? ""
              : result.error().message + " code=" +
                    std::to_string(static_cast<unsigned>(result.error().code)));
  return std::move(result.value());
}

template <typename Predicate>
void Until(Predicate predicate, const std::string& failure) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return;
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  throw std::runtime_error(failure);
}

inline Platform GetPlatform(Client& client, const std::string& id) {
  return Unwrap(
      Await<Platform>([&](auto done) { return client.GetPlatform(id, done); }));
}

inline Statistics Stats(Client& client) {
  return Unwrap(
      Await<Statistics>([&](auto done) { return client.GetStatistics(done); }));
}

inline Operation Catalog(Client& client, const std::string& id) {
  auto operation = Unwrap(Await<Operation>(
      [&](auto done) { return client.SyncCatalog(id, done); }));
  Until(
      [&] {
        operation = Unwrap(Await<Operation>([&](auto done) {
          return client.GetOperation(operation.id, done);
        }));
        return operation.state != OperationState::kRunning;
      },
      "catalog did not settle");
  return operation;
}

inline PlaybackLease Live(Client& client,
                          const ResourceId& id,
                          RtpReceiveMode mode) {
  return Unwrap(Await<PlaybackLease>(
      [&](auto done) { return client.StartLive({id, mode}, done); }));
}

inline Result<Playback> Ready(Client& client, const PlaybackId& id) {
  RequestOptions options;
  options.timeout = std::chrono::seconds(15);
  return Await<Playback>(
      [&](auto done) { return client.WaitPlaybackReady(id, done, options); });
}

inline void Release(Client& client, const PlaybackLease& lease) {
  Require(Await<void>([&](auto done) {
            return client.ReleasePlayback(lease.id, done);
          }).ok(),
          "release lease");
}

inline std::vector<Resource> Resources(Client& client, const PlatformId& id) {
  ResourceFilter filter;
  filter.platform_id = id;
  filter.limit = 1000;
  return Unwrap(Await<std::vector<Resource>>(
      [&](auto done) { return client.ListResources(filter, done); }));
}
}  // namespace gb28181::test
#endif  // GB28181_TESTS_SDK_FLOW_H_
