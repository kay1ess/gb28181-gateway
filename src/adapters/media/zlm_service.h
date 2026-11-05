#ifndef GB28181_SRC_ADAPTERS_MEDIA_ZLM_SERVICE_H_
#define GB28181_SRC_ADAPTERS_MEDIA_ZLM_SERVICE_H_
#include <memory>

#include "src/ports/media_service.h"

namespace gb28181::media {
struct ZlmState;

// The upstream C SDK has process-global options/listeners. Exactly one active
// instance is supported per process; a second Start returns kInvalidState.
class ZlmMediaService final : public IMediaService {
 public:
  ZlmMediaService();
  ~ZlmMediaService() override;
  Result<void> Start(const Config& config,
                     std::function<void(MediaEvent)> handler) override;
  void Stop() override;
  Result<Receiver> CreateReceiver(const ReceiverOptions& options) override;
  Result<void> ConnectReceiver(const ReceiverId& id,
                               const std::string& address,
                               uint16_t port) override;
  Result<void> CloseReceiver(const ReceiverId& id) override;
  Result<Receiver> GetReceiver(const ReceiverId& id) override;
  uint32_t ReceiverCount() const override;

 private:
  std::shared_ptr<ZlmState> state_;
};
}  // namespace gb28181::media
#endif  // GB28181_SRC_ADAPTERS_MEDIA_ZLM_SERVICE_H_
