#ifndef GB28181_SRC_ADAPTERS_SIP_RESIP_SERVICE_H_
#define GB28181_SRC_ADAPTERS_SIP_RESIP_SERVICE_H_
#include <memory>

#include "src/ports/sip_service.h"

namespace gb28181::sip {
class ResipService final : public IGb28181Service {
 public:
  ResipService();
  ~ResipService() override;
  Result<void> Start(const Config& config,
                     const std::vector<PlatformConfig>& platforms,
                     std::function<void(SipEvent)> handler) override;
  void Stop() override;
  Result<void> ReplacePlatforms(std::vector<PlatformConfig> platforms) override;
  Result<void> SendQuery(const PlatformId& platform_id,
                         const std::string& target_gb_id,
                         const std::string& command,
                         uint32_t sn,
                         const std::string& request_id) override;
  Result<void> InviteLive(LiveInvite request) override;
  Result<void> TerminateSession(const SipSessionId& id) override;
  SipStatistics GetStatistics() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace gb28181::sip
#endif  // GB28181_SRC_ADAPTERS_SIP_RESIP_SERVICE_H_
