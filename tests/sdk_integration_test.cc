#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "gb28181/client.h"
#include "tests/child_process.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using namespace std::chrono_literals;

void Check(bool condition, const std::string& message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename T, typename Invoke>
Result<T> Await(Invoke invoke) {
  auto promise = std::make_shared<std::promise<Result<T>>>();
  auto future = promise->get_future();
  auto accepted = invoke(
      [promise](Result<T> value) { promise->set_value(std::move(value)); });
  Check(accepted.ok(),
        accepted.ok() ? "" : "request rejected: " + accepted.error().message);
  Check(future.wait_for(20s) == std::future_status::ready,
        "SDK callback did not arrive");
  return future.get();
}

template <typename T>
T Value(Result<T> result) {
  if (!result)
    throw std::runtime_error(
        result.error().message +
        " code=" + std::to_string(static_cast<uint32_t>(result.error().code)));
  return std::move(result.value());
}

void WaitOnline(Client& client, const std::string& id) {
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto platform = Value(Await<Platform>(
        [&](auto done) { return client.GetPlatform(id, done); }));
    if (platform.state == OnlineState::kOnline)
      return;
    std::this_thread::sleep_for(25ms);
  }
  throw std::runtime_error("simulator did not register");
}

void Catalog(Client& client, const PlatformId& id) {
  auto operation = Value(Await<Operation>(
      [&](auto done) { return client.SyncCatalog(id, done); }));
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  while (std::chrono::steady_clock::now() < deadline) {
    operation = Value(Await<Operation>(
        [&](auto done) { return client.GetOperation(operation.id, done); }));
    if (operation.state == OperationState::kSucceeded) {
      Check(operation.received == 2, "catalog unique item count");
      return;
    }
    if (operation.state != OperationState::kRunning)
      throw std::runtime_error("catalog failed: " + operation.error.message);
    std::this_thread::sleep_for(25ms);
  }
  throw std::runtime_error("catalog did not complete");
}

void TestTransport(const std::string& simulator,
                   const std::string& sample,
                   const std::string& ffmpeg,
                   const std::filesystem::path& directory,
                   const std::string& transport) {
  Config config;
  config.rtp_port = test::UnusedRtpPort();
  config.bind_address = "127.0.0.1";
  config.media_http_port = 0;
  config.media_rtsp_port = 0;
  config.database_path =
      (directory / ("integration-" + transport + ".db")).string();
  config.sip_port = test::UnusedSipPort();
  auto client = CreateClient();
  auto started = client->Start(config);
  Check(started.ok(), "SDK start failed");
  PlatformConfig platform;
  platform.id = "lower_" + transport;
  platform.gb_id = "34020000002000000002";
  platform.password = "test-only";
  Value(Await<Platform>(
      [&](auto done) { return client->AddPlatform(platform, done); }));
  const bool independent = std::getenv("GB_PEER_KIND") &&
                           std::string(std::getenv("GB_PEER_KIND")) == "mickey";
  auto arguments = std::vector<std::string>{
      simulator,    "-server", "127.0.0.1:" + std::to_string(config.sip_port),
      "-transport", transport, "-media",
      sample};
  if (!independent)
    arguments.insert(arguments.end(),
                     {"-ffmpeg", ffmpeg, "-catalog-mode", "duplicate"});
  test::ChildProcess device(
      arguments, (directory / ("peer-" + transport + ".log")).string());
  const auto registration_begin = std::chrono::steady_clock::now();
  WaitOnline(*client, platform.id);
  const auto catalog_begin = std::chrono::steady_clock::now();
  Catalog(*client, platform.id);
  std::cout << "timing SIP=" << transport << " registration_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   catalog_begin - registration_begin)
                   .count()
            << " catalog_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - catalog_begin)
                   .count()
            << std::endl;
  ResourceFilter filter;
  filter.platform_id = platform.id;
  auto resources = Value(Await<std::vector<Resource>>(
      [&](auto done) { return client->ListResources(filter, done); }));
  Check(resources.size() == 2, "two shared channels persisted");
  Check(resources[0].platform_id == platform.id,
        "resource sharing source retained");
  const auto id = resources[0].id;
  auto uncached = Await<DeviceInfo>(
      [&](auto done) { return client->GetDeviceInfo(id, done); });
  Check(!uncached && uncached.error().code == ErrorCode::kNotFound,
        "unqueried cache is absent");
  auto info = Value(Await<DeviceInfo>(
      [&](auto done) { return client->RefreshDeviceInfo(id, done); }));
  Check(info.manufacturer == (independent ? "gb28181-go" : "LocalSimulator") &&
            info.queried_ms > 0,
        "device information query and freshness");
  auto status = Value(Await<DeviceStatus>(
      [&](auto done) { return client->RefreshDeviceStatus(id, done); }));
  Check(status.state == OnlineState::kOnline, "device status query");
  auto cached_info = Value(Await<DeviceInfo>(
      [&](auto done) { return client->GetDeviceInfo(id, done); }));
  auto cached_status = Value(Await<DeviceStatus>(
      [&](auto done) { return client->GetDeviceStatus(id, done); }));
  Check(cached_info.queried_ms == info.queried_ms &&
            cached_status.queried_ms == status.queried_ms,
        "cache retains original query freshness");
  for (auto mode : {RtpReceiveMode::kUdp, RtpReceiveMode::kTcpPassive,
                    RtpReceiveMode::kTcpActive}) {
    auto defaults =
        Value(Await<Platform>([&](auto done) {
          return client->GetPlatform(resources[0].platform_id, done);
        })).config;
    defaults.default_receive_mode = mode;
    Value(Await<Platform>(
        [&](auto done) { return client->UpdatePlatform(defaults, done); }));
    auto lease = Value(Await<PlaybackLease>(
        [&](auto done) { return client->StartLive({id}, done); }));
    defaults.default_receive_mode = mode == RtpReceiveMode::kUdp
                                        ? RtpReceiveMode::kTcpActive
                                        : RtpReceiveMode::kUdp;
    Value(Await<Platform>(
        [&](auto done) { return client->UpdatePlatform(defaults, done); }));
    auto shared = Value(Await<PlaybackLease>(
        [&](auto done) { return client->StartLive({id, mode}, done); }));
    Check(lease.id != shared.id && lease.playback_id == shared.playback_id,
          "multiple consumers share upstream session");
    RequestOptions options;
    options.timeout = 15s;
    auto playing = Value(Await<Playback>([&](auto done) {
      return client->WaitPlaybackReady(lease.playback_id, done, options);
    }));
    Check(playing.state == PlaybackState::kPlaying && playing.mode == mode &&
              !playing.endpoints.empty(),
          "media readiness from actual stream event");
    Check(playing.created_ms > 0 &&
              playing.negotiated_ms >= playing.created_ms &&
              playing.media_ready_ms >= playing.created_ms &&
              playing.ready_ms >= playing.negotiated_ms &&
              playing.ready_ms >= playing.media_ready_ms,
          "SIP and media phase timestamps");
    // Upstream device.Server owns a single media sender. Independent concurrent
    // channels are tested through its cascade.Service role in cascade_test.
    PlaybackLease other;
    Playback other_playing;
    if (!independent) {
      other = Value(Await<PlaybackLease>([&](auto done) {
        return client->StartLive({resources[1].id, mode}, done);
      }));
      other_playing = Value(Await<Playback>([&](auto done) {
        return client->WaitPlaybackReady(other.playback_id, done, options);
      }));
      Check(other_playing.ssrc != playing.ssrc,
            "Concurrent channels have independent SSRCs on the fixed listener");
    }
    const auto decoding_begin = std::chrono::steady_clock::now();
    test::ChildProcess decoder({ffmpeg, "-nostdin", "-hide_banner", "-loglevel",
                                "error", "-rtsp_transport", "tcp", "-i",
                                playing.endpoints[0].url, "-frames:v", "1",
                                "-f", "null", "-"});
    Check(decoder.Wait(15s) == 0,
          "FFmpeg could not decode one frame from SDK playback endpoint");
    std::cout << "timing RTP=" << static_cast<int>(mode) << " negotiation_ms="
              << playing.negotiated_ms - playing.created_ms
              << " valid_media_ms="
              << playing.media_ready_ms - playing.created_ms
              << " ready_ms=" << playing.ready_ms - playing.created_ms
              << " external_decode_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - decoding_begin)
                     .count()
              << std::endl;
    auto released = Await<void>(
        [&](auto done) { return client->ReleasePlayback(lease.id, done); });
    Check(released.ok(), "release first viewer");
    auto retained = Value(Await<Playback>([&](auto done) {
      return client->GetPlayback(shared.playback_id, done);
    }));
    Check(retained.state == PlaybackState::kPlaying && retained.consumers == 1,
          "remaining viewer retains media");
    released = Await<void>(
        [&](auto done) { return client->ReleasePlayback(shared.id, done); });
    Check(released.ok(), "release last viewer");
    auto ended = Value(Await<Playback>([&](auto done) {
      return client->GetPlayback(shared.playback_id, done);
    }));
    Check(ended.state == PlaybackState::kEnded, "last viewer ends playback");
    if (!independent) {
      test::ChildProcess other_decoder(
          {ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
           "-rtsp_transport", "tcp", "-i", other_playing.endpoints[0].url,
           "-frames:v", "1", "-f", "null", "-"});
      Check(other_decoder.Wait(15s) == 0,
            "Other channel still decodes after first channel is released");
      released = Await<void>(
          [&](auto done) { return client->ReleasePlayback(other.id, done); });
      Check(released.ok(), "release concurrent channel");
    }
    auto statistics = Value(Await<Statistics>(
        [&](auto done) { return client->GetStatistics(done); }));
    Check(statistics.active_receivers == 0 &&
              statistics.active_playbacks == 0 &&
              statistics.playback_leases == 0,
          "media resources return to baseline");
    const auto sip_deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < sip_deadline &&
           (statistics.sip_sessions || statistics.sip_requests)) {
      std::this_thread::sleep_for(10ms);
      statistics = Value(Await<Statistics>(
          [&](auto done) { return client->GetStatistics(done); }));
    }
    Check(statistics.sip_sessions == 0 && statistics.sip_requests == 0,
          "SIP sessions and pending requests return to baseline");
    std::cout << "passed SIP=" << transport
              << " media=" << static_cast<int>(mode) << std::endl;
  }
  auto stopped = client->Stop();
  Check(stopped.ok(), "SDK stop");
}
}  // namespace

int main() {
  try {
    const char* simulator = std::getenv("GB_SIMULATOR");
    const char* sample = std::getenv("GB_SAMPLE");
    const char* ffmpeg = std::getenv("GB_FFMPEG");
    const char* temporary = std::getenv("TEST_TMPDIR");
    Check(simulator && sample && ffmpeg,
          "set GB_SIMULATOR, GB_SAMPLE and GB_FFMPEG to prepared local test "
          "artifacts");
    auto directory = temporary ? std::filesystem::path(temporary)
                               : std::filesystem::temp_directory_path();
    TestTransport(simulator, sample, ffmpeg, directory, "udp");
    TestTransport(simulator, sample, ffmpeg, directory, "tcp");
    std::cout << "SDK end-to-end SIP UDP/TCP and three RTP modes passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
