#ifndef GB28181_SRC_PORTS_MEDIA_SERVICE_H_
#define GB28181_SRC_PORTS_MEDIA_SERVICE_H_
#include <functional>
#include <string>
#include <vector>

#include "gb28181/config.h"
#include "gb28181/result.h"
#include "gb28181/types.h"

namespace gb28181 {
struct ReceiverOptions {
  ReceiverId id;
  RtpReceiveMode mode = RtpReceiveMode::kUdp;
  // Zero uses the service's persistent port; any explicit value must match it.
  uint16_t port = 0;
  uint32_t ssrc = 0;
};

struct Receiver {
  ReceiverOptions options;
  uint16_t port = 0;
  bool ready = false;
  bool connected = false;
  uint64_t bytes_per_second = 0;
  std::vector<std::string> codecs;
  std::vector<PlaybackEndpoint> endpoints;
};
enum class MediaEventType {
  kReady,
  kClosed,
  kConnected,
  kConnectionFailed,
  kTimeout
};

struct MediaEvent {
  ReceiverId id;
  MediaEventType type = MediaEventType::kReady;
  Error error{ErrorCode::kOk, {}, {}};
};

class IMediaService {
 public:
  virtual ~IMediaService() = default;
  // Events originate on media threads. Handlers must only enqueue owned data;
  // never block or expose these threads to SDK callers. Late events are valid.
  virtual Result<void> Start(const Config& config,
                             std::function<void(MediaEvent)> handler) = 0;
  virtual void Stop() = 0;
  virtual Result<Receiver> CreateReceiver(const ReceiverOptions& options) = 0;
  virtual Result<void> ConnectReceiver(const ReceiverId& id,
                                       const std::string& address,
                                       uint16_t port) = 0;
  virtual Result<void> CloseReceiver(const ReceiverId& id) = 0;
  virtual Result<Receiver> GetReceiver(const ReceiverId& id) = 0;
  virtual Result<uint32_t> GetReaderCount(const ReceiverId& id) = 0;
  virtual uint32_t ReceiverCount() const = 0;
};
}  // namespace gb28181
#endif  // GB28181_SRC_PORTS_MEDIA_SERVICE_H_
