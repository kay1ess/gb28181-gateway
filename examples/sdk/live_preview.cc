#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "gb28181/client.h"

namespace {
using namespace gb28181;

template <typename T, typename Work>
T Wait(Work work) {
  auto promise = std::make_shared<std::promise<Result<T>>>();
  auto future = promise->get_future();
  auto accepted = work(
      [promise](Result<T> result) { promise->set_value(std::move(result)); });
  if (!accepted)
    throw std::runtime_error(accepted.error().message);
  if (future.wait_for(std::chrono::seconds(20)) != std::future_status::ready)
    throw std::runtime_error("SDK callback timeout");
  auto result = future.get();
  if (!result)
    throw std::runtime_error(result.error().message);
  return std::move(result.value());
}
}  // namespace

int main(int argc, char** argv) {
  using namespace gb28181;
  using namespace std::chrono_literals;
  auto client = CreateClient();
  try {
    Config config;
    config.database_path = argc > 1 ? argv[1] : "preview.db";
    config.bind_address = "127.0.0.1";
    config.media_http_port = 0;
    config.media_rtsp_port = 0;
    const std::string mode_name = argc > 2 ? argv[2] : "udp";
    RtpReceiveMode mode;
    if (mode_name == "udp")
      mode = RtpReceiveMode::kUdp;
    else if (mode_name == "tcp_passive")
      mode = RtpReceiveMode::kTcpPassive;
    else if (mode_name == "tcp_active")
      mode = RtpReceiveMode::kTcpActive;
    else
      throw std::runtime_error("mode: udp | tcp_passive | tcp_active");
    const auto started = client->Start(config);
    if (!started)
      throw std::runtime_error(started.error().message);
    PlatformConfig platform;
    platform.id = "example_lower";
    platform.gb_id = "34020000002000000002";
    platform.password = "test-only";
    // This executable is a local interoperability example. Production callers
    // supply their own provisioned identities and credentials.
    Wait<Platform>(
        [&](auto done) { return client->AddPlatform(platform, done); });
    std::cout << "Waiting for device 34020000002000000002 on SIP 5060; test "
                 "password: test-only\n";
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    for (;;) {
      auto state = Wait<Platform>(
          [&](auto done) { return client->GetPlatform(platform.id, done); });
      if (state.state == OnlineState::kOnline)
        break;
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("device did not register");
      std::this_thread::sleep_for(100ms);
    }
    auto operation = Wait<Operation>(
        [&](auto done) { return client->SyncCatalog(platform.id, done); });
    while (operation.state == OperationState::kRunning) {
      std::this_thread::sleep_for(50ms);
      operation = Wait<Operation>(
          [&](auto done) { return client->GetOperation(operation.id, done); });
    }
    if (operation.state != OperationState::kSucceeded)
      throw std::runtime_error(operation.error.message);
    auto resources = Wait<std::vector<Resource>>(
        [&](auto done) { return client->ListResources({}, done); });
    if (resources.empty())
      throw std::runtime_error("empty catalog");
    auto lease = Wait<PlaybackLease>([&](auto done) {
      StartLiveRequest request;
      request.platform_id = resources[0].platform_id;
      request.channel_gb_id = resources[0].gb_id;
      request.mode = mode;
      return client->StartLive(std::move(request), done);
    });
    auto playback = Wait<Playback>([&](auto done) {
      return client->WaitPlaybackReady(lease.playback_id, done);
    });
    for (const auto& endpoint : playback.endpoints)
      std::cout << endpoint.url << std::endl;
    // Demonstrate periodic renewal while the caller owns the playback lease.
    for (int count = 0; count < 3; ++count) {
      std::this_thread::sleep_for(5s);
      lease = Wait<PlaybackLease>(
          [&](auto done) { return client->RenewPlayback(lease.id, done); });
    }
    auto released = std::make_shared<std::promise<Result<void>>>();
    auto finished = released->get_future();
    if (!client->ReleasePlayback(lease.id, [released](auto result) {
          released->set_value(std::move(result));
        }))
      throw std::runtime_error("release rejected");
    if (finished.wait_for(10s) != std::future_status::ready || !finished.get())
      throw std::runtime_error("release failed");
    return client->Stop().ok() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    client->Stop();
    return 1;
  }
}
