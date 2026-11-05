#ifndef GB28181_INCLUDE_GB28181_EVENTS_H_
#define GB28181_INCLUDE_GB28181_EVENTS_H_
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gb28181/types.h"

namespace gb28181 {
enum class EventType {
  kPlatformChanged,
  kCatalogChanged,
  kOperationChanged,
  kPlaybackChanged
};

struct Event {
  EventType type = EventType::kPlatformChanged;
  std::string entity_id;
  std::string trace_id;
  Error error{ErrorCode::kOk, {}, {}};
  // Snapshot for kOperationChanged, including terminal status after storage.
  std::optional<Operation> operation = std::nullopt;
  // The accepted SIP batch before normalization and storage. Null for other
  // notifications. A retransmitted batch can contain duplicate resources.
  std::shared_ptr<const std::vector<Resource>> catalog_batch = nullptr;
};

using EventHandler = std::function<void(Event)>;
}  // namespace gb28181
#endif  // GB28181_INCLUDE_GB28181_EVENTS_H_
