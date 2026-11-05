#ifndef GB28181_SRC_ADAPTERS_SIP_XML_CODEC_H_
#define GB28181_SRC_ADAPTERS_SIP_XML_CODEC_H_
#include <cstdint>
#include <string>
#include <vector>

#include "gb28181/result.h"
#include "gb28181/types.h"

namespace gb28181::sip {
struct XmlMessage {
  std::string root;
  std::string command;
  uint32_t sn = 0;
  std::string device_id;
  uint32_t total = 0;
  std::vector<Resource> resources;
  DeviceInfo info;
  DeviceStatus status;
};

Result<XmlMessage> ParseXml(const std::string& body);
Result<std::string> BuildQuery(const std::string& command,
                               uint32_t sn,
                               const std::string& device_id);
}  // namespace gb28181::sip
#endif  // GB28181_SRC_ADAPTERS_SIP_XML_CODEC_H_
