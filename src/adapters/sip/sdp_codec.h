#ifndef GB28181_SRC_ADAPTERS_SIP_SDP_CODEC_H_
#define GB28181_SRC_ADAPTERS_SIP_SDP_CODEC_H_
#include <cstdint>
#include <string>

#include "gb28181/result.h"
#include "gb28181/types.h"

namespace gb28181::sip {
struct MediaDescription {
  std::string address;
  uint16_t port = 0;
  uint32_t ssrc = 0;
  bool tcp = false;
  std::string setup;
};

Result<std::string> BuildLiveSdp(const std::string& server_id,
                                 const std::string& address,
                                 uint16_t port,
                                 uint32_t ssrc,
                                 RtpReceiveMode mode);
Result<MediaDescription> ParseLiveAnswer(const std::string& body,
                                         RtpReceiveMode mode);
}  // namespace gb28181::sip
#endif  // GB28181_SRC_ADAPTERS_SIP_SDP_CODEC_H_
