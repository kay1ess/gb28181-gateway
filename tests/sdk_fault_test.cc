#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "src/adapters/storage/sqlite/database.h"
#include "tests/child_process.h"
#include "tests/sdk_flow.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using namespace gb28181::test;
using namespace std::chrono_literals;

const char* Env(const char* name) {
  auto value = std::getenv(name);
  Require(value && *value, std::string("missing ") + name);
  return value;
}

void Baseline(Client& client) {
  Until(
      [&] {
        const auto stats = Stats(client);
        return stats.active_playbacks == 0 && stats.active_receivers == 0 &&
               stats.playback_leases == 0 && stats.active_queries == 0 &&
               stats.sip_sessions == 0 && stats.sip_requests == 0;
      },
      "fault resource baseline, including SIP sessions and requests");
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
    config.query_timeout_ms = 1200;
    config.media_timeout_ms = 3500;
    config.lease_seconds = 10;
    config.max_playbacks = 2;
    config.database_path =
        (std::filesystem::path(Env("TEST_TMPDIR")) / "fault.db").string();
    auto client = CreateClient();
    Require(client->Start(config).ok(), "start fault SDK");
    PlatformConfig platform;
    platform.id = "lower";
    platform.gb_id = "34020000002000000002";
    platform.password = "test-only";
    platform.heartbeat_timeout_seconds = 2;
    Unwrap(Await<Platform>(
        [&](auto done) { return client->AddPlatform(platform, done); }));
    std::unique_ptr<ChildProcess> peer;
    auto restart = [&](const std::vector<std::string>& extra) {
      peer.reset();
      if (GetPlatform(*client, platform.id).state == OnlineState::kOnline)
        Until(
            [&] {
              return GetPlatform(*client, platform.id).state ==
                     OnlineState::kOffline;
            },
            "heartbeat expiry");
      auto args = std::vector<std::string>{
          Env("GB_SIMULATOR"),
          "-server",
          "127.0.0.1:" + std::to_string(config.sip_port),
          "-media",
          Env("GB_SAMPLE"),
          "-ffmpeg",
          Env("GB_FFMPEG"),
          "-expires",
          "6"};
      args.insert(args.end(), extra.begin(), extra.end());
      peer = std::make_unique<ChildProcess>(args);
      Until(
          [&] {
            return GetPlatform(*client, platform.id).state ==
                   OnlineState::kOnline;
          },
          "fault peer registration");
    };
    restart({});
    Require(Catalog(*client, platform.id).state == OperationState::kSucceeded,
            "seed catalog");
    const auto resources = Resources(*client, platform.id);
    Require(resources.size() == 2, "seed two resources");
    const auto id = resources[0].id;
    const auto info = Unwrap(Await<DeviceInfo>(
        [&](auto done) { return client->RefreshDeviceInfo(id, done); }));
    for (const std::string mode : {"missing", "conflict"}) {
      restart({"-catalog-mode", mode});
      const auto operation = Catalog(*client, platform.id);
      Require(operation.state == OperationState::kFailed,
              "incomplete/conflicting catalog must fail");
      Require(Resources(*client, platform.id).size() == 2,
              "failed catalog preserves old snapshot");
      Baseline(*client);
      std::cout << "passed catalog fault=" << mode
                << " code=" << static_cast<unsigned>(operation.error.code)
                << std::endl;
    }
    restart({"-invite-mode", "reject"});
    auto rejected = Live(*client, id, RtpReceiveMode::kUdp);
    auto result = Ready(*client, rejected.playback_id);
    Require(!result && result.error().code == ErrorCode::kSipRequestRejected,
            "INVITE rejection mapping");
    Baseline(*client);
    for (const std::string fault :
         {"no-answer", "bad-sdp", "connect-refused"}) {
      restart({"-invite-mode", fault});
      const auto mode = fault == "connect-refused" ? RtpReceiveMode::kTcpActive
                                                   : RtpReceiveMode::kUdp;
      const auto pending = Live(*client, id, mode);
      result = Ready(*client, pending.playback_id);
      const auto expected =
          fault == "bad-sdp"           ? ErrorCode::kSdpNegotiationFailed
          : fault == "connect-refused" ? ErrorCode::kMediaConnectFailed
                                       : ErrorCode::kMediaReceiveTimeout;
      if (!result)
        std::cout << "observed fault=" << fault
                  << " code=" << static_cast<unsigned>(result.error().code)
                  << std::endl;
      Require(!result && result.error().code == expected,
              "negotiation/connection fault is classified and never ready");
      Baseline(*client);
      std::cout << "passed signaling fault=" << fault
                << " code=" << static_cast<unsigned>(expected) << std::endl;
    }
    for (const std::string fault : {"no-media", "wrong-ssrc"}) {
      restart(fault == "no-media"
                  ? std::vector<std::string>{"-invite-mode", "no-media"}
                  : std::vector<std::string>{"-wrong-ssrc"});
      for (auto mode : {RtpReceiveMode::kUdp, RtpReceiveMode::kTcpPassive,
                        RtpReceiveMode::kTcpActive}) {
        auto lease = Live(*client, id, mode);
        result = Ready(*client, lease.playback_id);
        Require(
            !result && result.error().code == ErrorCode::kMediaReceiveTimeout,
            "missing/incorrect RTP must timeout without readiness");
        Baseline(*client);
        std::cout << "passed media fault=" << fault
                  << " mode=" << static_cast<int>(mode) << std::endl;
      }
    }
    restart({});
    const auto capacity_first = Live(*client, id, RtpReceiveMode::kUdp);
    const auto capacity_second =
        Live(*client, resources[1].id, RtpReceiveMode::kTcpPassive);
    Require(Ready(*client, capacity_first.playback_id).ok() &&
                Ready(*client, capacity_second.playback_id).ok(),
            "two receivers fill configured capacity");
    const auto exhausted = Await<PlaybackLease>([&](auto done) {
      return client->StartLive({id, RtpReceiveMode::kTcpActive}, done);
    });
    Require(
        !exhausted && exhausted.error().code == ErrorCode::kResourceExhausted,
        "capacity exhaustion returns explicit error before allocating");
    Release(*client, capacity_first);
    Release(*client, capacity_second);
    Baseline(*client);
    auto lease = Live(*client, id, RtpReceiveMode::kTcpPassive);
    Require(Ready(*client, lease.playback_id).ok(), "recovered playback ready");
    // Keep the peer alive across a renewal boundary; registration must remain
    // valid through new Digest challenges and refreshed source binding.
    std::this_thread::sleep_for(4s);
    Require(GetPlatform(*client, platform.id).state == OnlineState::kOnline,
            "renewed registration stays online");
    peer.reset();
    Until(
        [&] {
          return GetPlatform(*client, platform.id).state ==
                 OnlineState::kOffline;
        },
        "playing peer disconnect detection");
    Baseline(*client);
    auto cached = Unwrap(Await<DeviceInfo>(
        [&](auto done) { return client->GetDeviceInfo(id, done); }));
    Require(cached.queried_ms == info.queried_ms,
            "offline read retains cached freshness");
    auto failed_refresh = Await<DeviceInfo>(
        [&](auto done) { return client->RefreshDeviceInfo(id, done); });
    Require(!failed_refresh &&
                failed_refresh.error().code == ErrorCode::kPlatformOffline,
            "offline refresh fails without destroying cache");
    restart({});
    lease = Live(*client, id, RtpReceiveMode::kTcpActive);
    const auto recovered_playback = Unwrap(Ready(*client, lease.playback_id));
    lease = Unwrap(Await<PlaybackLease>(
        [&](auto done) { return client->RenewPlayback(lease.id, done); }));
    auto unexpected_offline = std::make_shared<std::atomic<bool>>(false);
    const auto subscription =
        Unwrap(client->SubscribeEvents([unexpected_offline](Event event) {
          if (event.type == EventType::kPlatformChanged &&
              event.error.code == ErrorCode::kPlatformOffline)
            unexpected_offline->store(true);
        }));
    // Real decoding must continue while a caller blocks the SDK callback
    // executor. A separate timer guarantees cleanup even if decoding fails.
    auto gate = std::make_shared<std::promise<void>>();
    auto gate_future = gate->get_future().share();
    auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    auto unblock = std::async(std::launch::async, [gate] {
      std::this_thread::sleep_for(8s);
      gate->set_value();
    });
    Require(client
                ->GetPlatform(platform.id,
                              [gate_future, entered](auto) {
                                entered->set_value();
                                gate_future.wait();
                              })
                .ok(),
            "slow callback accepted");
    Require(entered_future.wait_for(3s) == std::future_status::ready,
            "slow callback entered");
    {
      ChildProcess decoder({Env("GB_FFMPEG"), "-nostdin", "-hide_banner",
                            "-loglevel", "error", "-rtsp_transport", "tcp",
                            "-i", recovered_playback.endpoints[0].url,
                            "-frames:v", "1", "-f", "null", "-"});
      Require(decoder.Wait(6s) == 0,
              "media continues through blocked SDK callback");
    }
    unblock.get();
    Require(GetPlatform(*client, platform.id).state == OnlineState::kOnline,
            "heartbeat survives slow callback");
    lease = Unwrap(Await<PlaybackLease>(
        [&](auto done) { return client->RenewPlayback(lease.id, done); }));
    // Use a real competing SQLite writer, rather than adding delay branches
    // to production storage. Protocol and media work must remain responsive.
    {
      storage::Database competing;
      competing.Open(config.database_path);
      storage::Transaction transaction(competing);
      auto promise = std::make_shared<std::promise<Result<Platform>>>();
      auto stored = promise->get_future();
      auto updated = platform;
      updated.name = "blocked writer";
      Require(client
                  ->UpdatePlatform(updated,
                                   [promise](auto result) {
                                     promise->set_value(std::move(result));
                                   })
                  .ok(),
              "busy writer accepted");
      ChildProcess decoder({Env("GB_FFMPEG"), "-nostdin", "-hide_banner",
                            "-loglevel", "error", "-rtsp_transport", "tcp",
                            "-i", recovered_playback.endpoints[0].url,
                            "-frames:v", "1", "-f", "null", "-"});
      Require(decoder.Wait(6s) == 0,
              "media continues through SQLite writer contention");
      Require(stored.wait_for(2s) == std::future_status::ready,
              "busy writer has bounded completion");
      const auto busy = stored.get();
      Require(!busy && busy.error().code == ErrorCode::kStorageBusy,
              "busy writer SDK error");
      Require(GetPlatform(*client, platform.id).state == OnlineState::kOnline,
              "heartbeat survives busy storage queue");
    }
    Require(!unexpected_offline->load(),
            "no transient offline event during slow callback or storage");
    client->UnsubscribeEvents(subscription);
    Require(Await<void>([&](auto done) {
              return client->ForceStopPlayback(lease.playback_id, done);
            }).ok(),
            "force stop");
    Require(Await<void>([&](auto done) {
              return client->ForceStopPlayback(lease.playback_id, done);
            }).ok(),
            "repeated force stop");
    Baseline(*client);
    lease = Live(*client, id, RtpReceiveMode::kUdp);
    Require(Ready(*client, lease.playback_id).ok(),
            "lease-expiry playback ready");
    Until([&] { return Stats(*client).playback_leases == 0; },
          "abandoned lease expiration");
    Baseline(*client);
    // Accepted waiter cancellation cannot stop another viewer's lease.
    const auto status_file =
        std::filesystem::path(Env("TEST_TMPDIR")) / "late-answer.json";
    restart({"-invite-delay", "500ms", "-status-file", status_file.string()});
    int late_count = 0;
    for (auto mode : {RtpReceiveMode::kUdp, RtpReceiveMode::kTcpPassive,
                      RtpReceiveMode::kTcpActive}) {
      lease = Live(*client, id, mode);
      auto completions = std::make_shared<std::atomic<int>>(0);
      auto promise = std::make_shared<std::promise<Result<Playback>>>();
      auto waiting = promise->get_future();
      Require(client
                  ->WaitPlaybackReady(lease.playback_id,
                                      [promise, completions](auto result) {
                                        if (++*completions == 1)
                                          promise->set_value(std::move(result));
                                      })
                  .ok(),
              "late-answer waiter accepted");
      std::this_thread::sleep_for(30ms);
      Release(*client, lease);
      Require(waiting.wait_for(3s) == std::future_status::ready,
              "released waiter settles");
      Require(!waiting.get(), "release cancels media waiter");
      ++late_count;
      Until(
          [&] {
            std::ifstream file(status_file);
            const std::string status((std::istreambuf_iterator<char>(file)),
                                     {});
            return status.find("\"late_answers\":" +
                               std::to_string(late_count)) != std::string::npos;
          },
          "simulator did not inject late INVITE success");
      Baseline(*client);
      Require(completions->load() == 1,
              "late answer cannot complete a waiter twice");
      const auto ended = Unwrap(Await<Playback>([&](auto done) {
        return client->GetPlayback(lease.playback_id, done);
      }));
      Require(ended.state == PlaybackState::kEnded && ended.endpoints.empty(),
              "late success cannot revive released playback");
    }
    restart({"-invite-mode", "no-media", "-catalog-mode", "missing"});
    lease = Live(*client, id, RtpReceiveMode::kTcpPassive);
    RequestOptions options;
    Cancellation cancel;
    options.cancellation = cancel;
    auto promise = std::make_shared<std::promise<Result<Playback>>>();
    auto waiting = promise->get_future();
    Require(
        client
            ->WaitPlaybackReady(
                lease.playback_id,
                [promise](auto value) { promise->set_value(std::move(value)); },
                options)
            .ok(),
        "accept cancellable waiter");
    cancel.Cancel();
    Require(waiting.wait_for(3s) == std::future_status::ready,
            "cancel waiter settles");
    auto cancelled = waiting.get();
    Require(!cancelled && cancelled.error().code == ErrorCode::kCancelled,
            "waiter cancellation error");
    Require(Stats(*client).playback_leases == 1,
            "wait cancellation preserves playback owner");
    auto query = Unwrap(Await<Operation>(
        [&](auto done) { return client->SyncCatalog(platform.id, done); }));
    Require(client->Stop().ok(), "close with active playback/query");
    Require(client->Start(config).ok(), "restart after active close");
    Require(GetPlatform(*client, platform.id).state == OnlineState::kUnknown,
            "restart never restores online state");
    Baseline(*client);
    Require(Resources(*client, platform.id).size() == 2,
            "restart preserves catalog");
    Require(client->Stop().ok(), "final stop");
    std::cout << "SDK fault recovery, cache, lease expiry, cancellation and "
                 "shutdown passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
