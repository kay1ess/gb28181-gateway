#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "src/adapters/media/zlm_service.h"
#include "tests/child_process.h"
#include "tests/test_support.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using namespace gb28181::test;
using namespace std::chrono_literals;

const char* Env(const char* name) {
  const char* value = std::getenv(name);
  Require(value && *value, std::string("Missing ") + name);
  return value;
}

// Both SSRCs intentionally use the very same UDP source endpoint. Continue
// sending released/unknown SSRCs to exercise admission and listener isolation.
class Sender {
 public:
  Sender(uint16_t port, std::string sample)
      : thread_([this, port, sample] {
          try {
            UdpPeer peer;
            size_t offset = 0;
            uint16_t sequence = 0;
            const auto started = std::chrono::steady_clock::now();
            while (!stop_) {
              const auto count = std::min(size_t{1200}, sample.size() - offset);
              std::string packet(12 + count, '\0');
              packet[0] = static_cast<char>(0x80);
              packet[1] = static_cast<char>(0x80 | 96);
              const auto seq = htons(sequence++);
              const auto timestamp = htonl(static_cast<uint32_t>(
                  std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - started)
                      .count() *
                  90 / 1000));
              memcpy(packet.data() + 2, &seq, sizeof(seq));
              memcpy(packet.data() + 4, &timestamp, sizeof(timestamp));
              packet.replace(12, count, sample, offset, count);
              for (uint32_t ssrc : {101u, 202u, 999u}) {
                const auto value = htonl(ssrc);
                memcpy(packet.data() + 8, &value, sizeof(value));
                peer.Send(port, packet);
              }
              offset = (offset + count) % sample.size();
              std::this_thread::sleep_for(2ms);
            }
          } catch (...) {
            failed_ = true;
          }
        }) {
  }

  ~Sender() {
    stop_ = true;
    thread_.join();
  }

  bool failed() const {
    return failed_;
  }

 private:
  std::atomic<bool> stop_{false};
  std::atomic<bool> failed_{false};
  std::thread thread_;
};

Receiver Ready(media::ZlmMediaService& service, const std::string& id) {
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  while (std::chrono::steady_clock::now() < deadline) {
    auto receiver = service.GetReceiver(id);
    RequireOk(receiver, "Get admitted stream");
    if (receiver.value().ready)
      return receiver.value();
    std::this_thread::sleep_for(20ms);
  }
  throw std::runtime_error("Shared UDP stream failed to become ready: " + id);
}

void Decode(const Receiver& receiver) {
  ChildProcess decoder({Env("GB_FFMPEG"), "-nostdin", "-hide_banner",
                        "-loglevel", "error", "-rtsp_transport", "tcp", "-i",
                        receiver.endpoints[0].url, "-frames:v", "1", "-f",
                        "null", "-"});
  Require(decoder.Wait(15s) == 0,
          "Shared UDP endpoint decodes an actual frame");
}
}  // namespace

int main() {
  try {
    std::ifstream file(Env("GB_SAMPLE"), std::ios::binary);
    std::string sample((std::istreambuf_iterator<char>(file)), {});
    Require(!sample.empty(), "PS fixture exists");
    Config config;
    config.bind_address = "127.0.0.1";
    config.sip_port = 0;
    config.rtp_port = 0;
    config.media_http_port = 0;
    config.media_rtsp_port = 0;
    media::ZlmMediaService service;
    RequireOk(service.Start(config, {}), "Start shared listener");
    const auto a =
        service.CreateReceiver({"same_source_a", RtpReceiveMode::kUdp, 0, 101});
    const auto b =
        service.CreateReceiver({"same_source_b", RtpReceiveMode::kUdp, 0, 202});
    RequireOk(a, "Admit first SSRC");
    RequireOk(b, "Admit second SSRC");
    Require(a.value().port == b.value().port,
            "Both cameras use one destination port");
    {
      Sender sender(a.value().port, sample);
      Decode(Ready(service, "same_source_a"));
      Decode(Ready(service, "same_source_b"));
      RequireOk(service.CloseReceiver("same_source_a"), "Release first SSRC");
      Decode(Ready(service, "same_source_b"));
      Require(service.ReceiverCount() == 1,
              "Unknown/released SSRC cannot create receiver");
      RequireOk(service.CloseReceiver("same_source_b"), "Release second SSRC");
      std::this_thread::sleep_for(200ms);
      Require(service.ReceiverCount() == 0,
              "Old packets cannot revive released streams");
      Require(!sender.failed(), "Same-source UDP sender remains healthy");
    }
    RequireOk(service.CreateReceiver({"replay", RtpReceiveMode::kUdp, 0, 101}),
              "Re-admit released SSRC");
    {
      Sender sender(a.value().port, sample);
      Decode(Ready(service, "replay"));
    }
    service.Stop();
    std::cout << "Single UDP source, two admitted SSRCs, real decode, release "
                 "isolation and replay passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
