#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "tests/child_process.h"
#include "tests/sdk_flow.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using namespace gb28181::test;
using namespace std::chrono_literals;

const char* Env(const char* name) {
  const auto value = std::getenv(name);
  Require(value && *value, std::string("missing ") + name);
  return value;
}

void Consumers(const std::filesystem::path& path, int expected) {
  Until(
      [&] {
        std::ifstream file(path);
        const std::string body((std::istreambuf_iterator<char>(file)), {});
        return body.find("\"consumers\":" + std::to_string(expected)) !=
               std::string::npos;
      },
      "lower cascade source consumer count mismatch");
}
}  // namespace

int main() {
  try {
    const std::filesystem::path directory = Env("TEST_TMPDIR");
    const char* output_env = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    const auto output_directory =
        output_env ? std::filesystem::path(output_env) : directory;
    const auto status = directory / "cascade-status.json";
    Config config;
    config.rtp_port = UnusedRtpPort();
    config.bind_address = "127.0.0.1";
    config.sip_port = UnusedSipPort();
    config.media_http_port = 0;
    config.media_rtsp_port = 0;
    config.database_path = (directory / "cascade.db").string();
    auto client = CreateClient();
    Require(client->Start(config).ok(), "cascade upper SDK start");
    PlatformConfig platform;
    platform.id = "go_cascade";
    platform.gb_id = "34020000002000000002";
    platform.password = "test-only";
    Unwrap(Await<Platform>(
        [&](auto done) { return client->AddPlatform(platform, done); }));
    ChildProcess lower(
        {Env("GB_SIMULATOR"), "-role", "cascade", "-server",
         "127.0.0.1:" + std::to_string(config.sip_port), "-media",
         Env("GB_SAMPLE"), "-status-file", status.string()},
        (output_directory / "cascade-peer.log").string());
    Until(
        [&] {
          return GetPlatform(*client, platform.id).state ==
                 OnlineState::kOnline;
        },
        "real Go cascade registration");
    Require(Catalog(*client, platform.id).state == OperationState::kSucceeded,
            "real cascade catalog");
    const auto resources = Resources(*client, platform.id);
    Require(
        resources.size() == 2 && resources[0].manufacturer == "FixtureCamera",
        "independent lower platform aggregates two cameras");
    Require(resources[0].name == "级联摄像头 1" &&
                resources[1].name == "级联摄像头 2",
            "legacy encoded Chinese names survive catalog persistence");
    for (const auto mode :
         {RtpReceiveMode::kUdp, RtpReceiveMode::kTcpPassive}) {
      auto first = Live(*client, resources[0].id, mode);
      auto second = Live(*client, resources[1].id, mode);
      const auto a = Unwrap(Ready(*client, first.playback_id));
      const auto b = Unwrap(Ready(*client, second.playback_id));
      Require(a.id != b.id && a.ssrc != b.ssrc,
              "cascade channels have distinct sessions and SSRC");
      Consumers(status, 2);
      for (const auto& playback : {a, b}) {
        ChildProcess decoder({Env("GB_FFMPEG"), "-nostdin", "-hide_banner",
                              "-loglevel", "error", "-rtsp_transport", "tcp",
                              "-i", playback.endpoints[0].url, "-frames:v", "1",
                              "-f", "null", "-"});
        Require(decoder.Wait(15s) == 0, "real cascade forwarded video decodes");
      }
      Release(*client, first);
      Consumers(status, 1);
      Require(Stats(*client).active_receivers == 1,
              "other cascaded camera retains its receiver");
      Release(*client, second);
      Consumers(status, 0);
      Until(
          [&] {
            const auto stats = Stats(*client);
            return !stats.active_receivers && !stats.sip_sessions &&
                   !stats.sip_requests;
          },
          "cascade resources including SIP return to zero");
      std::cout << "passed lower platform cascade mode="
                << static_cast<int>(mode)
                << " channels=2 concurrent, decoded, lower consumers=0"
                << std::endl;
    }
    Require(client->Stop().ok(), "cascade upper SDK Stop");
    std::cout << "Independent gb28181-go cascade.Service platform interop "
                 "passed; physical vendor NVR remains unverified\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    if (const char* output = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR")) {
      std::ifstream peer(std::filesystem::path(output) / "cascade-peer.log");
      std::cerr << peer.rdbuf();
    }
    return 1;
  }
}
