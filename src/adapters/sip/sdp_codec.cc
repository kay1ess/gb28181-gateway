#include "src/adapters/sip/sdp_codec.h"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <set>
#include <sstream>

namespace gb28181::sip {
namespace {
Error Invalid(const char* message) {
  return {ErrorCode::kSdpNegotiationFailed, message, {}};
}

bool Number(const std::string& text, uint32_t* value) {
  if (text.empty())
    return false;
  auto result = std::from_chars(text.data(), text.data() + text.size(), *value);
  return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool Address(const std::string& text) {
  std::istringstream stream(text);
  std::string part;
  int count = 0;
  while (std::getline(stream, part, '.')) {
    uint32_t value = 0;
    if (!Number(part, &value) || value > 255)
      return false;
    ++count;
  }
  return count == 4 && !text.empty() && text.back() != '.' && text != "0.0.0.0";
}
}  // namespace

Result<std::string> BuildLiveSdp(const std::string& server_id,
                                 const std::string& address,
                                 uint16_t port,
                                 uint32_t ssrc,
                                 RtpReceiveMode mode) {
  if (server_id.size() != 20 ||
      !std::all_of(server_id.begin(), server_id.end(),
                   [](char c) { return c >= '0' && c <= '9'; }) ||
      !Address(address) || !port || !ssrc)
    return Invalid("Invalid local SDP endpoint");
  std::ostringstream body;
  body << "v=0\r\no=" << server_id << " 0 0 IN IP4 " << address
       << "\r\ns=Play\r\nc=IN IP4 " << address << "\r\nt=0 0\r\nm=video "
       << port << (mode == RtpReceiveMode::kUdp ? " RTP/AVP" : " TCP/RTP/AVP")
       << " 96\r\na=recvonly\r\na=rtpmap:96 PS/90000\r\n";
  if (mode != RtpReceiveMode::kUdp)
    body << "a=setup:"
         << (mode == RtpReceiveMode::kTcpActive ? "active" : "passive")
         << "\r\na=connection:new\r\n";
  body << "y=" << std::setw(10) << std::setfill('0') << ssrc << "\r\n";
  return body.str();
}

Result<MediaDescription> ParseLiveAnswer(const std::string& body,
                                         RtpReceiveMode mode) {
  if (body.empty() || body.size() > 65536)
    return Invalid("Invalid SDP size");
  MediaDescription media;
  std::istringstream lines(body);
  std::string line, session_address;
  bool in_video = false, found_video = false;
  bool ps = false;
  std::set<std::string> payloads;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.rfind("m=", 0) == 0) {
      in_video = line.rfind("m=video ", 0) == 0;
      if (!in_video)
        continue;
      if (found_video)
        return Invalid("Multiple video descriptions");
      found_video = true;
      std::istringstream fields(line.substr(2));
      std::string kind, port, protocol, payload;
      fields >> kind >> port >> protocol;
      uint32_t number = 0;
      if (!Number(port, &number) || number == 0 || number > 65535 ||
          (protocol != "RTP/AVP" && protocol != "TCP/RTP/AVP"))
        return Invalid("Invalid video transport");
      media.port = static_cast<uint16_t>(number);
      media.tcp = protocol == "TCP/RTP/AVP";
      while (fields >> payload)
        payloads.insert(payload);
    } else if (line.rfind("c=IN IP4 ", 0) == 0) {
      if (in_video)
        media.address = line.substr(9);
      else if (!found_video)
        session_address = line.substr(9);
    } else if (in_video && line.rfind("a=setup:", 0) == 0) {
      media.setup = line.substr(8);
    } else if (in_video && line.rfind("a=rtpmap:", 0) == 0) {
      std::istringstream fields(line.substr(9));
      std::string payload, encoding;
      fields >> payload >> encoding;
      if (payloads.count(payload) && encoding == "PS/90000")
        ps = true;
    } else if (line.rfind("y=", 0) == 0) {
      if (!Number(line.substr(2), &media.ssrc))
        return Invalid("Invalid SDP SSRC");
    }
  }
  if (media.address.empty())
    media.address = session_address;
  if (!found_video || !Address(media.address) || !media.ssrc || !ps)
    return Invalid("Missing PS video endpoint or SSRC");
  if (media.tcp != (mode != RtpReceiveMode::kUdp))
    return Invalid("Answer transport differs from offer");
  if (mode == RtpReceiveMode::kTcpActive && media.setup != "passive")
    return Invalid("Expected a passive TCP sender");
  if (mode == RtpReceiveMode::kTcpPassive && media.setup != "active")
    return Invalid("Expected an active TCP sender");
  return media;
}
}  // namespace gb28181::sip
