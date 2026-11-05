#ifndef GB28181_SRC_PORTS_SIP_SERVICE_H_
#define GB28181_SRC_PORTS_SIP_SERVICE_H_
#include <functional>
#include <string>
#include <vector>

#include "gb28181/config.h"
#include "gb28181/result.h"
#include "gb28181/types.h"

namespace gb28181 {
enum class SipEventType {
  kRegistered,
  kUnregistered,
  kHeartbeat,
  kQueryResponse,
  kRequestFailed,
  kInviteAnswer,
  kSessionEnded
};

struct SipEvent {
  SipEventType type = SipEventType::kRequestFailed;
  PlatformId platform_id;
  std::string request_id;
  std::string command;
  uint32_t sn = 0;
  uint32_t catalog_total = 0;
  std::string device_gb_id;
  std::vector<Resource> resources;
  DeviceInfo info;
  DeviceStatus status;
  std::string remote_address;
  uint16_t remote_port = 0;
  std::string transport;
  uint32_t expires_seconds = 0;
  uint32_t ssrc = 0;
  Error error{ErrorCode::kOk, {}, {}};
};

struct LiveInvite {
  SipSessionId session_id;
  PlatformId platform_id;
  std::string device_gb_id;
  std::string receive_address;
  uint16_t receive_port = 0;
  uint32_t ssrc = 0;
  RtpReceiveMode mode = RtpReceiveMode::kUdp;
};

struct SipStatistics {
  uint32_t sessions = 0;
  uint32_t requests = 0;
};

class IGb28181Service {
 public:
  virtual ~IGb28181Service() = default;
  // Internal events arrive on the SIP thread; handlers must enqueue and return.
  // Accepted commands settle through events; the application owns deadlines.
  virtual Result<void> Start(const Config& config,
                             const std::vector<PlatformConfig>& platforms,
                             std::function<void(SipEvent)> handler) = 0;
  virtual void Stop() = 0;
  virtual Result<void> ReplacePlatforms(
      std::vector<PlatformConfig> platforms) = 0;
  virtual Result<void> SendQuery(const PlatformId& platform_id,
                                 const std::string& target_gb_id,
                                 const std::string& command,
                                 uint32_t sn,
                                 const std::string& request_id) = 0;
  virtual Result<void> InviteLive(LiveInvite request) = 0;
  virtual Result<void> TerminateSession(const SipSessionId& id) = 0;
  // Thread-safe last snapshot. Completed transaction timers are not sessions.
  virtual SipStatistics GetStatistics() const = 0;
};
}  // namespace gb28181
#endif  // GB28181_SRC_PORTS_SIP_SERVICE_H_
