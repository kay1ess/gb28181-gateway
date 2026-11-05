#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <thread>

#include "gb28181/client.h"
#include "tests/test_support.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using test::Require;
using test::RequireOk;

template <typename T, typename Invoke>
Result<T> Await(Invoke invoke) {
  auto promise = std::make_shared<std::promise<Result<T>>>();
  auto future = promise->get_future();
  RequireOk(invoke([promise](Result<T> result) {
              promise->set_value(std::move(result));
            }),
            "SDK accepted operation");
  Require(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
          "SDK callback deadline");
  return future.get();
}
}  // namespace

int main() {
  using namespace std::chrono_literals;
  auto client = CreateClient();
  Config config;
  config.rtp_port = 0;
  config.bind_address = "127.0.0.1";
  config.media_http_port = 0;
  config.media_rtsp_port = 0;
  config.sip_port = test::UnusedSipPort();
  const char* temporary = std::getenv("TEST_TMPDIR");
  const auto directory = temporary ? std::filesystem::path(temporary)
                                   : std::filesystem::temp_directory_path();
  config.database_path = (directory / "sdk-test.db").string();
  auto invalid = config;
  invalid.server_gb_id = "not-a-gb-code";
  Require(!client->Start(invalid).ok(), "invalid SDK configuration rejected");
  invalid = config;
  invalid.database_path = (directory / "missing-parent" / "sdk.db").string();
  const auto failed_open = client->Start(invalid);
  Require(
      !failed_open &&
          failed_open.error().code == ErrorCode::kStorageMigrationFailed,
      "unwritable database initialization cannot report SDK startup success");
  RequireOk(client->Start(config), "start real SDK adapters");
  Require(!client->Start(config).ok(), "duplicate Start rejected");
  std::atomic<int> notifications{0};
  std::atomic<bool> callback_stop_rejected{false};
  auto subscription = client->SubscribeEvents([&](Event) {
    ++notifications;
    callback_stop_rejected = !client->Stop().ok();
  });
  RequireOk(subscription, "subscribe SDK events");
  PlatformConfig platform;
  platform.id = "lower_1";
  platform.gb_id = "34020000002000000002";
  platform.password = "test-only";
  platform.name = "下级平台";
  auto added = Await<Platform>(
      [&](auto done) { return client->AddPlatform(platform, done); });
  RequireOk(added, "persist platform through SDK");
  Require(added.value().state == OnlineState::kUnknown,
          "configured platform is not automatically online");
  Require(callback_stop_rejected, "Stop in callback rejected without deadlock");
  auto duplicate = Await<Platform>(
      [&](auto done) { return client->AddPlatform(platform, done); });
  Require(!duplicate && duplicate.error().code == ErrorCode::kAlreadyExists &&
              !duplicate.error().trace_id.empty(),
          "stable error and trace ID");
  platform.name = "updated";
  RequireOk(Await<Platform>([&](auto done) {
              return client->UpdatePlatform(platform, done);
            }),
            "update platform");
  auto listed = Await<std::vector<Platform>>(
      [&](auto done) { return client->ListPlatforms(done); });
  RequireOk(listed, "list platforms");
  Require(
      listed.value().size() == 1 && listed.value()[0].config.name == "updated",
      "platform cache reconciled after commit");
  auto offline = Await<Operation>(
      [&](auto done) { return client->SyncCatalog(platform.id, done); });
  Require(!offline && offline.error().code == ErrorCode::kPlatformOffline,
          "offline query fails explicitly");
  RequestOptions cancelled;
  cancelled.cancellation.Cancel();
  auto cancellation = Await<Platform>([&](auto done) {
    return client->GetPlatform(platform.id, done, cancelled);
  });
  Require(!cancellation && cancellation.error().code == ErrorCode::kCancelled,
          "accepted cancelled operation settles");
  RequestOptions zero;
  zero.timeout = 0ms;
  Require(!client->GetPlatform(
                     platform.id, [](auto) {}, zero)
               .ok(),
          "invalid request rejected synchronously");
  client->UnsubscribeEvents(subscription.value());
  const int before = notifications.load();
  platform.name = "after unsubscribe";
  RequireOk(Await<Platform>([&](auto done) {
              return client->UpdatePlatform(platform, done);
            }),
            "update after unsubscribe");
  Require(notifications == before, "unsubscribed callback suppressed");
  auto resources = Await<std::vector<Resource>>(
      [&](auto done) { return client->ListResources({}, done); });
  RequireOk(resources, "read persistent directory through storage executor");
  Require(resources.value().empty(), "initial resource directory empty");
  auto stats =
      Await<Statistics>([&](auto done) { return client->GetStatistics(done); });
  RequireOk(stats, "SDK diagnostics");
  Require(stats.value().active_receivers == 0, "no media allocated by CRUD");

  // Hold one user callback while Stop settles a burst of accepted operations.
  // This checks the callback barrier and prevents a false pass from fast tasks.
  auto gate = std::make_shared<std::promise<void>>();
  auto gate_future = gate->get_future().share();
  auto entered = std::make_shared<std::promise<void>>();
  auto entered_future = entered->get_future();
  RequireOk(client->GetPlatform(platform.id,
                                [entered, gate_future](Result<Platform>) {
                                  entered->set_value();
                                  gate_future.wait();
                                }),
            "enqueue blocking callback");
  Require(entered_future.wait_for(3s) == std::future_status::ready,
          "callback entered");
  std::atomic<int> completions{0};
  for (int index = 0; index < 100; ++index)
    RequireOk(client->GetPlatform(platform.id,
                                  [&](Result<Platform>) { ++completions; }),
              "accepted burst operation");
  auto stop = std::async(std::launch::async, [&] { return client->Stop(); });
  Require(stop.wait_for(50ms) == std::future_status::timeout,
          "Stop waits for running callback");
  gate->set_value();
  Require(stop.wait_for(5s) == std::future_status::ready,
          "Stop has bounded internal shutdown");
  RequireOk(stop.get(), "Stop completes");
  Require(completions == 100,
          "every accepted operation settles exactly once before Stop returns");
  Require(!client->ListPlatforms([](auto) {}).ok(),
          "stopped SDK rejects commands");
  RequireOk(client->Start(config), "restart SDK");
  auto restored = Await<Platform>(
      [&](auto done) { return client->GetPlatform(platform.id, done); });
  RequireOk(restored, "persistent configuration restored");
  Require(restored.value().config.name == platform.name &&
              restored.value().state == OnlineState::kUnknown,
          "restart does not resurrect online status");
  RequireOk(Await<void>([&](auto done) {
              return client->RemovePlatform(platform.id, done);
            }),
            "delete platform");
  platform.id.clear();
  auto generated = Await<Platform>(
      [&](auto done) { return client->AddPlatform(platform, done); });
  RequireOk(generated, "SDK generates omitted platform ID");
  const auto generated_id = generated.value().config.id;
  Require(generated_id.find("lower_") == 0,
          "generated platform ID has an internal namespace");
  platform.gb_id = "34020000002000000003";
  auto second = Await<Platform>(
      [&](auto done) { return client->AddPlatform(platform, done); });
  RequireOk(second, "create another generated platform");
  Require(second.value().config.id != generated_id,
          "generated platform IDs are distinct");
  auto invalid_update = Await<Platform>(
      [&](auto done) { return client->UpdatePlatform(platform, done); });
  Require(!invalid_update &&
              invalid_update.error().code == ErrorCode::kInvalidArgument,
          "updates never generate a replacement ID");
  RequireOk(client->Stop(), "stop before restoring generated ID");
  RequireOk(client->Start(config), "restart with generated IDs");
  auto persisted = Await<Platform>(
      [&](auto done) { return client->GetPlatform(generated_id, done); });
  RequireOk(persisted, "generated ID persists across restart");
  Require(persisted.value().config.id == generated_id,
          "restart preserves generated identity");
  RequireOk(client->Stop(), "final stop");
  std::cout << "SDK real-adapter lifecycle and CRUD tests passed\n";
}
