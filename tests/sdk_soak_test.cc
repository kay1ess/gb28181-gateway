#include "tests/child_process.h"
#include "tests/sdk_flow.h"
#include "tests/udp_peer.h"
#ifdef _WIN32
#include <psapi.h>
#include <tlhelp32.h>
#endif
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>

namespace {
using namespace gb28181;
using namespace gb28181::test;
using namespace std::chrono_literals;

const char* Env(const char* name) {
  auto value = std::getenv(name);
  Require(value && *value, std::string("missing ") + name);
  return value;
}

struct Metrics {
  uint64_t bytes = 0;
  uint32_t handles = 0;
  uint32_t threads = 0;
};

Metrics Measure() {
  Metrics result;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  Require(
      GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                           sizeof(memory)) != 0,
      "process memory metrics");
  result.bytes = memory.PrivateUsage;
  DWORD handles = 0;
  Require(GetProcessHandleCount(GetCurrentProcess(), &handles) != 0,
          "handle metrics");
  result.handles = handles;
  auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  Require(snapshot != INVALID_HANDLE_VALUE, "thread snapshot");
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Thread32First(snapshot, &entry))
    do {
      if (entry.th32OwnerProcessID == GetCurrentProcessId())
        ++result.threads;
    } while (Thread32Next(snapshot, &entry));
  CloseHandle(snapshot);
#endif
  return result;
}

void Print(const std::string& phase, Client& client, const Config& config) {
  const auto metrics = Measure();
  const auto stats = Stats(client);
  std::cout << "metrics phase=" << phase << " private_bytes=" << metrics.bytes
            << " handles=" << metrics.handles << " threads=" << metrics.threads
            << " receivers=" << stats.active_receivers
            << " playbacks=" << stats.active_playbacks
            << " leases=" << stats.playback_leases
            << " queries=" << stats.active_queries
            << " sip_sessions=" << stats.sip_sessions
            << " sip_requests=" << stats.sip_requests
            << " db_bytes=" << std::filesystem::file_size(config.database_path)
            << std::endl;
  const auto wal = config.database_path + "-wal";
  std::cout << "wal_bytes="
            << (std::filesystem::exists(wal) ? std::filesystem::file_size(wal)
                                             : 0)
            << std::endl;
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
    config.lease_seconds = 120;
    config.database_path =
        (std::filesystem::path(Env("TEST_TMPDIR")) / "soak.db").string();
    auto client = CreateClient();
    Require(client->Start(config).ok(), "start soak SDK");
    PlatformConfig platform;
    platform.id = "soak_lower";
    platform.gb_id = "34020000002000000002";
    platform.password = "test-only";
    platform.heartbeat_timeout_seconds = 3;
    Unwrap(Await<Platform>(
        [&](auto done) { return client->AddPlatform(platform, done); }));
    auto start_peer = [&] {
      return std::make_unique<ChildProcess>(std::vector<std::string>{
          Env("GB_SIMULATOR"), "-server",
          "127.0.0.1:" + std::to_string(config.sip_port), "-transport", "tcp",
          "-media", Env("GB_SAMPLE"), "-ffmpeg", Env("GB_FFMPEG")});
    };
    auto peer = start_peer();
    Until(
        [&] {
          return GetPlatform(*client, platform.id).state ==
                 OnlineState::kOnline;
        },
        "soak registration");
    Require(Catalog(*client, platform.id).state == OperationState::kSucceeded,
            "soak catalog");
    const auto resources = Resources(*client, platform.id);
    Require(resources.size() == 2, "two concurrent channels required");
    const std::array<RtpReceiveMode, 3> modes{RtpReceiveMode::kUdp,
                                              RtpReceiveMode::kTcpPassive,
                                              RtpReceiveMode::kTcpActive};
    Print("start", *client, config);
    for (auto mode : modes) {
      for (int cycle = 1; cycle <= 100; ++cycle) {
        auto lease = Live(*client, resources[0].id, mode);
        Require(Ready(*client, lease.playback_id).ok(),
                "cycle media readiness");
        Release(*client, lease);
        Until(
            [&] {
              auto stats = Stats(*client);
              return !stats.sip_sessions && !stats.sip_requests;
            },
            "cycle SIP resource baseline");
        const auto stats = Stats(*client);
        Require(stats.active_receivers == 0 && stats.active_playbacks == 0 &&
                    stats.playback_leases == 0,
                "cycle resource baseline");
        if (cycle % 10 == 0)
          Print("mode_" + std::to_string(static_cast<int>(mode)) + "_cycle_" +
                    std::to_string(cycle),
                *client, config);
      }
    }
    const auto baseline = Measure();
    Print("cycles_complete", *client, config);
    std::array<PlaybackLease, 2> leases;
    for (size_t index = 0; index < leases.size(); ++index) {
      leases[index] = Live(*client, resources[index].id, modes[index]);
      Require(Ready(*client, leases[index].playback_id).ok(),
              "concurrent media readiness");
    }
    const auto began = std::chrono::steady_clock::now();
    const auto finish = began + 1h;
    auto next_sample = began;
    auto next_restart = began + 10s;
    auto next_catalog = began + 1min;
    bool recovered = false;
    uint32_t restarts = 0;
    std::mt19937 random(28181);
    while (std::chrono::steady_clock::now() < finish) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_catalog) {
        Require(
            Catalog(*client, platform.id).state == OperationState::kSucceeded,
            "periodic catalog during media");
        next_catalog = now + 1min;
      }
      if (!recovered && now - began >= 30min) {
        peer.reset();
        Until(
            [&] {
              return GetPlatform(*client, platform.id).state ==
                     OnlineState::kOffline;
            },
            "soak disconnect detection");
        Require(Stats(*client).active_receivers == 0,
                "disconnect resource release");
        peer = start_peer();
        Until(
            [&] {
              return GetPlatform(*client, platform.id).state ==
                     OnlineState::kOnline;
            },
            "soak reconnect registration");
        for (size_t index = 0; index < leases.size(); ++index) {
          leases[index] = Live(*client, resources[index].id, modes[index]);
          Require(Ready(*client, leases[index].playback_id).ok(),
                  "soak recovery media");
        }
        recovered = true;
        std::cout << "disconnect and recovery passed" << std::endl;
      }
      if (now >= next_restart) {
        const size_t index = random() % leases.size();
        Release(*client, leases[index]);
        leases[index] =
            Live(*client, resources[index].id, modes[random() % modes.size()]);
        Require(Ready(*client, leases[index].playback_id).ok(),
                "random replay readiness");
        ++restarts;
        next_restart = now + std::chrono::seconds(8 + random() % 8);
      }
      if (now >= next_sample) {
        for (auto& lease : leases)
          lease = Unwrap(Await<PlaybackLease>([&](auto done) {
            return client->RenewPlayback(lease.id, done);
          }));
        Print("second_" + std::to_string(
                              std::chrono::duration_cast<std::chrono::seconds>(
                                  now - began)
                                  .count()),
              *client, config);
        const auto current = Measure();
        Require(current.bytes <= baseline.bytes + 256ULL * 1024 * 1024 &&
                    current.handles <= baseline.handles + 256 &&
                    current.threads <= baseline.threads + 32,
                "soak resource growth exceeds recorded guardrails");
        next_sample = now + 30s;
      }
      std::this_thread::sleep_for(100ms);
    }
    for (auto& lease : leases)
      Release(*client, lease);
    Require(Stats(*client).active_receivers == 0 &&
                Stats(*client).playback_leases == 0,
            "final soak baseline");
    Print("complete", *client, config);
    Require(client->Stop().ok(), "soak Stop");
    std::cout << "PASS 100 cycles per RTP mode; one hour, two concurrent "
                 "channels; random replays="
              << restarts << "; disconnect recovered=" << recovered
              << std::endl;
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << std::endl;
    return 1;
  }
}
