#ifndef GB28181_INCLUDE_GB28181_CONFIG_H_
#define GB28181_INCLUDE_GB28181_CONFIG_H_
#include <cstdint>
#include <string>

namespace gb28181 {
struct Config {
  std::string database_path = "gb28181.db";
  std::string server_gb_id = "34020000002000000001";
  std::string realm = "3402000000";
  std::string bind_address = "0.0.0.0";
  std::string advertised_address = "127.0.0.1";
  uint16_t sip_port = 5060;
  uint16_t media_http_port = 8081;
  uint16_t media_rtsp_port = 8554;
  // One persistent UDP/TCP-passive RTP listener; RTCP uses rtp_port + 1.
  // Zero selects a shared port once at Start, not once per playback.
  uint16_t rtp_port = 10000;
  uint32_t query_timeout_ms = 10000;
  uint32_t media_timeout_ms = 10000;
  uint32_t lease_seconds = 60;
  uint32_t max_playbacks = 64;
};
}  // namespace gb28181
#endif  // GB28181_INCLUDE_GB28181_CONFIG_H_
