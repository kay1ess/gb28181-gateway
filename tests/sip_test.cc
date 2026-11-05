#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <rutil/DigestStream.hxx>

#include "src/adapters/sip/resip_service.h"
#include "tests/test_support.h"
#include "tests/udp_peer.h"

namespace {
using namespace gb28181;
using test::Require;

std::string Hash(const std::string& input) {
  resip::DigestStream stream;
  stream << input;
  stream.flush();
  auto data = stream.getHex();
  return std::string(data.data(), data.size());
}

std::string Attribute(const std::string& message, const std::string& key) {
  const std::string prefix = key + "=\"";
  const auto start = message.find(prefix);
  Require(start != std::string::npos, "missing digest parameter " + key);
  const auto end = message.find('"', start + prefix.size());
  Require(end != std::string::npos, "invalid digest parameter " + key);
  return message.substr(start + prefix.size(), end - start - prefix.size());
}

std::string Request(const Config& config,
                    const PlatformConfig& platform,
                    uint16_t port,
                    int sequence,
                    const std::string& method,
                    const std::string& authorization = {},
                    const std::string& body = {},
                    unsigned expires = 3600) {
  const std::string address = "sip:" + platform.gb_id + "@" + config.realm;
  std::ostringstream request;
  request << method << " sip:" << config.server_gb_id << "@" << config.realm
          << " SIP/2.0\r\nVia: SIP/2.0/UDP 127.0.0.1:" << port
          << ";branch=z9hG4bK-test-" << sequence << ";rport\r\nFrom: <"
          << address << ">;tag=testtag\r\nTo: <" << address << ">\r\nCall-ID: "
          << (method == "REGISTER" ? "test-register"
                                   : "test-message-" + std::to_string(sequence))
          << "\r\nCSeq: " << sequence << " " << method
          << "\r\nMax-Forwards: 70\r\nContact: <sip:" << platform.gb_id
          << "@127.0.0.1:" << port << ">\r\nExpires: " << expires << "\r\n";
  if (!authorization.empty())
    request << "Authorization: " << authorization << "\r\n";
  if (!body.empty())
    request << "Content-Type: Application/MANSCDP+xml\r\n";
  request << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  return request.str();
}

std::string Authorization(const Config& config,
                          const PlatformConfig& platform,
                          const std::string& nonce,
                          const std::string& password) {
  const std::string uri = "sip:" + config.server_gb_id + "@" + config.realm;
  const auto response =
      Hash(Hash(platform.gb_id + ":" + config.realm + ":" + password) + ":" +
           nonce + ":00000001:clientnonce:auth:" + Hash("REGISTER:" + uri));
  return "Digest username=\"" + platform.gb_id + "\", realm=\"" + config.realm +
         "\", nonce=\"" + nonce + "\", uri=\"" + uri + "\", response=\"" +
         response +
         "\", algorithm=MD5, qop=auth, nc=00000001, cnonce=\"clientnonce\"";
}
}  // namespace

int main() {
  using namespace gb28181;
  Config config;
  config.rtp_port = 0;
  config.bind_address = "127.0.0.1";
  config.sip_port = test::UnusedSipPort();
  PlatformConfig platform;
  platform.id = "test-platform";
  platform.gb_id = "34020000002000000002";
  platform.password = "test-secret";
  std::mutex mutex;
  std::condition_variable wake;
  std::vector<SipEvent> events;
  sip::ResipService service;
  test::RequireOk(service.Start(config, {platform},
                                [&](SipEvent event) {
                                  std::lock_guard<std::mutex> lock(mutex);
                                  events.push_back(std::move(event));
                                  wake.notify_all();
                                }),
                  "start SIP UDP and TCP transports");
  test::UdpPeer device;
  device.Send(config.sip_port,
              Request(config, platform, device.port(), 1, "REGISTER"));
  const auto challenge = device.Receive();
  Require(challenge.rfind("SIP/2.0 401", 0) == 0,
          "REGISTER challenged with 401");
  const auto nonce = Attribute(challenge, "nonce");
  device.Send(config.sip_port,
              Request(config, platform, device.port(), 2, "REGISTER",
                      Authorization(config, platform, nonce, "incorrect")));
  const auto rejected = device.Receive();
  Require(rejected.rfind("SIP/2.0 200", 0) != 0, "bad password is rejected");
  device.Send(
      config.sip_port,
      Request(config, platform, device.port(), 3, "REGISTER",
              Authorization(config, platform, nonce, platform.password)));
  const auto accepted = device.Receive();
  Require(accepted.rfind("SIP/2.0 200", 0) == 0, "correct digest registers");
  const std::string heartbeat =
      "<Notify><CmdType>Keepalive</CmdType><SN>1</SN><DeviceID>" +
      platform.gb_id + "</DeviceID><Status>OK</Status></Notify>";
  device.Send(config.sip_port, Request(config, platform, device.port(), 4,
                                       "MESSAGE", {}, heartbeat));
  const auto heartbeat_response = device.Receive();
  Require(heartbeat_response.rfind("SIP/2.0 200", 0) == 0,
          "registered source heartbeat accepted: " + heartbeat_response);
  test::RequireOk(service.SendQuery(platform.id, platform.gb_id, "Catalog", 17,
                                    "identity_query"),
                  "send catalog to registered device");
  const auto catalog_request = device.Receive();
  Require(catalog_request.rfind("MESSAGE ", 0) == 0 &&
              catalog_request.find("From: <sip:" + config.server_gb_id + "@" +
                                   config.realm + ">") != std::string::npos,
          "outbound query carries the upper platform identity in From");
  std::string query_response = "SIP/2.0 200 OK\r\n";
  std::istringstream headers(catalog_request);
  std::string line;
  while (std::getline(headers, line) && line != "\r") {
    for (const char* name : {"Via:", "From:", "To:", "Call-ID:", "CSeq:"}) {
      if (line.rfind(name, 0) == 0)
        query_response += line + "\n";
    }
  }
  device.Send(config.sip_port, query_response + "Content-Length: 0\r\n\r\n");
  {
    std::unique_lock<std::mutex> lock(mutex);
    Require(wake.wait_for(lock, std::chrono::seconds(3),
                          [&] { return events.size() >= 2; }),
            "registration and heartbeat events");
    Require(events[0].type == SipEventType::kRegistered &&
                events[1].type == SipEventType::kHeartbeat,
            "event order");
  }
  test::UdpPeer forged;
  forged.Send(config.sip_port, Request(config, platform, forged.port(), 5,
                                       "MESSAGE", {}, heartbeat));
  Require(forged.Receive().rfind("SIP/2.0 403", 0) == 0,
          "unregistered endpoint cannot impersonate peer");
  int sequence = 6;
  auto register_peer = [&](const PlatformConfig& candidate,
                           const std::string& password, unsigned expires) {
    device.Send(config.sip_port, Request(config, candidate, device.port(),
                                         sequence++, "REGISTER"));
    const auto response = device.Receive();
    Require(response.rfind("SIP/2.0 401", 0) == 0,
            "every new registration is challenged");
    device.Send(
        config.sip_port,
        Request(config, candidate, device.port(), sequence++, "REGISTER",
                Authorization(config, candidate, Attribute(response, "nonce"),
                              password),
                {}, expires));
    return device.Receive();
  };
  auto message = [&](const std::string& body) {
    device.Send(config.sip_port, Request(config, platform, device.port(),
                                         sequence++, "MESSAGE", {}, body));
    return device.Receive();
  };
  PlatformConfig unknown = platform;
  unknown.gb_id = "34020000002000000099";
  Require(
      register_peer(unknown, platform.password, 3600).rfind("SIP/2.0 200", 0) !=
          0,
      "unknown lower platform rejected");
  Require(message("<Notify>").rfind("SIP/2.0 400", 0) == 0,
          "malformed XML rejected without losing registration");
  Require(message(heartbeat).rfind("SIP/2.0 200", 0) == 0,
          "valid heartbeat survives malformed input");
  device.Send(config.sip_port,
              Request(config, platform, device.port(), sequence++, "PUBLISH"));
  Require(device.Receive().rfind("SIP/2.0 405", 0) == 0,
          "unsupported SIP method is explicitly rejected");
  Require(
      register_peer(platform, platform.password, 0).rfind("SIP/2.0 200", 0) ==
          0,
      "authenticated unregister accepted");
  Require(message(heartbeat).rfind("SIP/2.0 403", 0) == 0,
          "unregistered peer loses MESSAGE authorization");
  Require(
      register_peer(platform, platform.password, 1).rfind("SIP/2.0 200", 0) ==
          0,
      "short registration accepted");
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  Require(message(heartbeat).rfind("SIP/2.0 403", 0) == 0,
          "expired registration cannot keep peer authorized");
  auto disabled = platform;
  disabled.enabled = false;
  test::RequireOk(service.ReplacePlatforms({disabled}), "disable peer");
  Require(register_peer(platform, platform.password, 3600)
                  .rfind("SIP/2.0 200", 0) != 0,
          "disabled lower platform rejected");
  auto changed = platform;
  changed.password = "replacement-secret";
  test::RequireOk(service.ReplacePlatforms({changed}), "change credentials");
  Require(register_peer(platform, platform.password, 3600)
                  .rfind("SIP/2.0 200", 0) != 0,
          "old password rejected after configuration update");
  Require(
      register_peer(platform, changed.password, 3600).rfind("SIP/2.0 200", 0) ==
          0,
      "updated password registers");
  changed.password = "third-secret";
  test::RequireOk(service.ReplacePlatforms({changed}), "invalidate old peer");
  Require(message(heartbeat).rfind("SIP/2.0 403", 0) == 0,
          "credential changes invalidate an already registered endpoint");
  service.Stop();
  Require(!service.SendQuery(platform.id, platform.gb_id, "Catalog", 1, "late")
               .ok(),
          "stopped adapter rejects queries");
  test::RequireOk(service.Start(config, {platform}, {}),
                  "restart SIP service and rebind ports");
  service.Stop();
  std::cout << "SIP digest registration and heartbeat tests passed\n";
}
