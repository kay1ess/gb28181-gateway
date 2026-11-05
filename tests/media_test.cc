#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "src/adapters/media/zlm_service.h"
#include "tests/test_support.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using test::Require;
using test::RequireOk;

uint16_t AvailablePair() {
  for (int attempt = 0; attempt < 512; ++attempt) {
    const auto port = test::UnusedSipPort();
    try {
      test::UdpPeer rtp(port);
      test::UdpPeer rtcp(static_cast<uint16_t>(port + 1));
      return port;
    } catch (const std::runtime_error&) {
    }
  }
  throw std::runtime_error("No RTP/RTCP pair available");
}

void Released(uint16_t port) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    try {
      test::UdpPeer rtp(port);
      test::UdpPeer rtcp(static_cast<uint16_t>(port + 1));
      if (rtp.CanBindTcp())
        return;
    } catch (const std::runtime_error&) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  Require(false, "Stop releases UDP RTP, RTCP and TCP listener");
}

void StillBound(uint16_t port) {
  bool occupied = false;
  try {
    test::UdpPeer probe(port);
  } catch (const std::runtime_error&) {
    occupied = true;
  }
  Require(occupied, "Shared listener persists without receivers");
}
}  // namespace

int main() {
  using namespace gb28181;
  media::ZlmMediaService service;
  Config config;
  config.bind_address = "127.0.0.1";
  config.sip_port = 0;
  config.media_http_port = 0;
  config.media_rtsp_port = 0;
  config.rtp_port = AvailablePair();
  config.max_playbacks = 3;
  RequireOk(service.Start(config, {}), "Start fixed shared listener");
  media::ZlmMediaService duplicate;
  Require(!duplicate.Start(config, {}), "Process engine has one owner");
  unsigned index = 0;
  for (auto mode : {RtpReceiveMode::kUdp, RtpReceiveMode::kTcpPassive,
                    RtpReceiveMode::kTcpActive}) {
    ReceiverOptions options;
    options.id = "receiver_" + std::to_string(++index);
    options.mode = mode;
    options.ssrc = index;
    const auto receiver = service.CreateReceiver(options);
    RequireOk(receiver, "Register SSRC");
    Require(receiver.value().port == config.rtp_port,
            "All modes advertise the same configured RTP port");
    Require(!receiver.value().ready, "Registration is not media readiness");
    Require(receiver.value().endpoints.size() == 2 &&
                receiver.value().endpoints[0].protocol == "rtsp" &&
                receiver.value().endpoints[1].protocol == "http-flv" &&
                receiver.value().endpoints[1].url.find(
                    "/rtp/" + options.id + ".live.flv") != std::string::npos,
            "RTSP and browser HTTP-FLV playback URLs returned");
    Require(!service.CreateReceiver(options), "Duplicate id rejected");
  }
  const auto duplicate_ssrc =
      service.CreateReceiver({"duplicate", RtpReceiveMode::kUdp, 0, 1});
  Require(!duplicate_ssrc &&
              duplicate_ssrc.error().code == ErrorCode::kAlreadyExists,
          "Different stream cannot claim an admitted SSRC");
  const auto excess =
      service.CreateReceiver({"excess", RtpReceiveMode::kUdp, 0, 5});
  Require(!excess && excess.error().code == ErrorCode::kResourceExhausted,
          "Capacity enforced");
  RequireOk(service.CloseReceiver("receiver_1"), "Close one stream");
  RequireOk(service.CloseReceiver("receiver_1"), "Close idempotent");
  const auto different_port =
      service.CreateReceiver({"different_port", RtpReceiveMode::kUdp,
                              static_cast<uint16_t>(config.rtp_port + 2), 8});
  Require(!different_port &&
              different_port.error().code == ErrorCode::kInvalidArgument,
          "Per-stream port override cannot create another listener");
  RequireOk(service.CloseReceiver("receiver_2"), "Close TCP passive");
  RequireOk(service.CloseReceiver("receiver_3"), "Close TCP active");
  Require(service.ReceiverCount() == 0, "Registrations removed");
  StillBound(config.rtp_port);
  RequireOk(service.CreateReceiver(
                {"reused", RtpReceiveMode::kUdp, config.rtp_port, 1}),
            "Released SSRC and explicit shared port can be reused");
  service.Stop();
  Released(config.rtp_port);
  Require(!service.CreateReceiver({"late", RtpReceiveMode::kUdp, 0, 9}),
          "Stopped service rejects registration");
  for (int conflict = 0; conflict < 3; ++conflict) {
    config.rtp_port = AvailablePair();
    {
      std::unique_ptr<test::UdpPeer> udp;
      std::unique_ptr<test::TcpPortBlocker> tcp;
      if (conflict == 2)
        tcp = std::make_unique<test::TcpPortBlocker>(config.rtp_port);
      else
        udp = std::make_unique<test::UdpPeer>(
            static_cast<uint16_t>(config.rtp_port + conflict));
      for (int attempt = 0; attempt < 3; ++attempt) {
        const auto failed = service.Start(config, {});
        Require(
            !failed && failed.error().code == ErrorCode::kMediaAllocationFailed,
            "Occupied fixed RTP/RTCP/TCP port fails without fallback");
        Require(service.ReceiverCount() == 0, "Failed start has no receivers");
      }
    }
    Released(config.rtp_port);
    RequireOk(service.Start(config, {}), "Restart after bind conflict");
    service.Stop();
    Released(config.rtp_port);
  }
  config.rtp_port = 65535;
  Require(!service.Start(config, {}), "RTCP overflow rejected");
  config.rtp_port = 0;
  RequireOk(service.Start(config, {}), "Choose shared port once at startup");
  const auto first = service.CreateReceiver({"a", RtpReceiveMode::kUdp, 0, 1});
  const auto second =
      service.CreateReceiver({"b", RtpReceiveMode::kTcpPassive, 0, 2});
  RequireOk(first, "Auto shared first registration");
  RequireOk(second, "Auto shared second registration");
  Require(first.value().port && first.value().port == second.value().port,
          "Zero chooses one shared port rather than per-stream ports");
  const auto auto_port = first.value().port;
  service.Stop();
  Released(auto_port);
  std::cout << "Fixed shared RTP lifecycle and bind rollback passed\n";
}
