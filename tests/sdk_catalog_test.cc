#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>

#include "tests/child_process.h"
#include "tests/sdk_flow.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using namespace gb28181::test;

const char* Env(const char* name) {
  auto value = std::getenv(name);
  Require(value && *value, std::string("missing ") + name);
  return value;
}

Operation Finished(Client& client, Operation operation) {
  Until(
      [&] {
        operation = Unwrap(Await<Operation>([&](auto done) {
          return client.GetOperation(operation.id, done);
        }));
        return operation.state != OperationState::kRunning;
      },
      "concurrent catalog did not settle");
  return operation;
}
}  // namespace

int main() {
  try {
    Config config;
    config.rtp_port = 0;
    config.bind_address = "127.0.0.1";
    config.sip_port = UnusedSipPort();
    config.media_http_port = 0;
    config.media_rtsp_port = 0;
    config.query_timeout_ms = 2500;
    config.database_path =
        (std::filesystem::path(Env("TEST_TMPDIR")) / "catalog.db").string();
    auto client = CreateClient();
    Require(client->Start(config).ok(), "catalog SDK start");
    std::vector<PlatformConfig> platforms(2);
    std::vector<std::unique_ptr<ChildProcess>> peers(2);
    for (int index = 0; index < 2; ++index) {
      auto& platform = platforms[index];
      platform.id = "lower_" + std::to_string(index);
      platform.gb_id = "3402000000200000000" + std::to_string(index + 2);
      platform.password = "test-only";
      platform.heartbeat_timeout_seconds = 2;
      Unwrap(Await<Platform>(
          [&](auto done) { return client->AddPlatform(platform, done); }));
    }
    auto restart = [&](int index, const std::vector<std::string>& extra) {
      peers[index].reset();
      if (GetPlatform(*client, platforms[index].id).state ==
          OnlineState::kOnline)
        Until(
            [&] {
              return GetPlatform(*client, platforms[index].id).state ==
                     OnlineState::kOffline;
            },
            "catalog peer offline");
      std::vector<std::string> args{
          Env("GB_SIMULATOR"), "-server",
          "127.0.0.1:" + std::to_string(config.sip_port), "-device-id",
          platforms[index].gb_id};
      args.insert(args.end(), extra.begin(), extra.end());
      peers[index] = std::make_unique<ChildProcess>(args);
      Until(
          [&] {
            return GetPlatform(*client, platforms[index].id).state ==
                   OnlineState::kOnline;
          },
          "catalog peer online");
    };
    restart(0, {});
    restart(1, {});
    auto first = Unwrap(Await<Operation>(
        [&](auto done) { return client->SyncCatalog(platforms[0].id, done); }));
    auto second = Unwrap(Await<Operation>(
        [&](auto done) { return client->SyncCatalog(platforms[1].id, done); }));
    Require(Finished(*client, first).state == OperationState::kSucceeded &&
                Finished(*client, second).state == OperationState::kSucceeded,
            "two simultaneous catalog queries");
    const auto a = Resources(*client, platforms[0].id);
    const auto b = Resources(*client, platforms[1].id);
    Require(a.size() == 2 && b.size() == 2 && a[0].gb_id == b[0].gb_id &&
                a[0].id != b[0].id,
            "identical GB channel codes retain two sharing sources");
    Require(a[0].parent_gb_id == platforms[0].gb_id &&
                b[0].parent_gb_id == platforms[1].gb_id,
            "parent linkage belongs to corresponding lower platform");
    // First query remains incomplete. The second peer returns the same SN
    // but conflicting totals; it must not fail or complete the first query.
    restart(0, {"-catalog-mode", "missing"});
    restart(1, {"-catalog-mode", "conflict", "-response-sn-offset", "-1"});
    first = Unwrap(Await<Operation>(
        [&](auto done) { return client->SyncCatalog(platforms[0].id, done); }));
    second = Unwrap(Await<Operation>(
        [&](auto done) { return client->SyncCatalog(platforms[1].id, done); }));
    first = Finished(*client, first);
    second = Finished(*client, second);
    Require(first.error.code == ErrorCode::kTimeout &&
                second.error.code == ErrorCode::kTimeout,
            "same-SN foreign source is ignored rather than corrupting another "
            "query");
    Require(Resources(*client, platforms[0].id).size() == 2 &&
                Resources(*client, platforms[1].id).size() == 2,
            "both failed catalogs preserve their prior snapshots");
    restart(0, {"-channels", "0"});
    Require(
        Catalog(*client, platforms[0].id).state == OperationState::kSucceeded,
        "zero-item remote catalog succeeds");
    Require(Resources(*client, platforms[0].id).empty() &&
                Resources(*client, platforms[1].id).size() == 2,
            "empty snapshot prunes exactly its own sharing source");
    restart(1, {"-channels", "1000", "-catalog-batch", "17"});

    struct Progress {
      std::mutex mutex;
      std::condition_variable changed;
      std::vector<Event> events;
    };

    auto progress = std::make_shared<Progress>();
    const auto subscription =
        Unwrap(client->SubscribeEvents([progress](Event event) {
          if (event.type != EventType::kOperationChanged || !event.operation)
            return;
          std::lock_guard<std::mutex> lock(progress->mutex);
          progress->events.push_back(std::move(event));
          progress->changed.notify_all();
        }));
    const auto began = std::chrono::steady_clock::now();
    const auto large = Unwrap(Await<Operation>(
        [&](auto done) { return client->SyncCatalog(platforms[1].id, done); }));
    {
      std::unique_lock<std::mutex> lock(progress->mutex);
      Require(progress->changed.wait_for(
                  lock, std::chrono::seconds(10),
                  [&] {
                    bool received_batch = false;
                    for (const auto& event : progress->events) {
                      const auto& snapshot = *event.operation;
                      if (snapshot.id != large.id || snapshot.expected != 1000)
                        continue;
                      if (snapshot.state == OperationState::kRunning &&
                          snapshot.received > 0 && snapshot.received < 1000 &&
                          event.catalog_batch &&
                          event.catalog_batch->size() == 17 &&
                          !event.catalog_batch->front().gb_id.empty())
                        received_batch = true;
                      if (snapshot.state == OperationState::kSucceeded &&
                          snapshot.received == 1000)
                        return received_batch;
                    }
                    return false;
                  }),
              "SDK callback delivers catalog progress and completion");
    }
    client->UnsubscribeEvents(subscription);
    const auto synchronized = std::chrono::steady_clock::now();
    auto stored = Resources(*client, platforms[1].id);
    Require(stored.size() == 1000, "all 1000 resources persisted");
    std::cout << "1000-channel sync ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     synchronized - began)
                     .count()
              << " SDK list us="
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::steady_clock::now() - synchronized)
                     .count()
              << std::endl;
    restart(0, {"-catalog-tree", "-catalog-batch", "1"});
    Require(
        Catalog(*client, platforms[0].id).state == OperationState::kSucceeded,
        "multi-level catalog completes across three batches");
    const auto tree = Resources(*client, platforms[0].id);
    Require(tree.size() == 3, "folder and two cameras persisted");
    Resource folder;
    for (const auto& resource : tree)
      if (resource.parental)
        folder = resource;
    Require(!folder.id.empty() && folder.parent_gb_id == platforms[0].gb_id,
            "folder belongs to its lower platform");
    for (const auto& resource : tree)
      if (!resource.parental)
        Require(resource.parent_gb_id == folder.gb_id,
                "camera retains nested folder identity");
    const auto folder_playback = Await<PlaybackLease>([&](auto done) {
      return client->StartLive({folder.id, RtpReceiveMode::kUdp}, done);
    });
    Require(!folder_playback &&
                folder_playback.error().code == ErrorCode::kInvalidArgument &&
                Stats(*client).active_receivers == 0,
            "directory group cannot allocate a camera playback");
    Require(client->Stop().ok(), "catalog SDK stop");
    std::cout
        << "multi-source, same-SN isolation, empty and large catalog passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
