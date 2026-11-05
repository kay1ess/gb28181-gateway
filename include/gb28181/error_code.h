#ifndef GB28181_INCLUDE_GB28181_ERROR_CODE_H_
#define GB28181_INCLUDE_GB28181_ERROR_CODE_H_
#include <cstdint>

namespace gb28181 {
enum class ErrorCode : uint32_t {
  kOk = 0,
  kInvalidArgument = 1001,
  kUnauthenticated = 1002,
  kPermissionDenied = 1003,
  kNotFound = 1004,
  kAlreadyExists = 1005,
  kInvalidState = 1006,
  kCancelled = 1007,
  kTimeout = 1008,
  kResourceExhausted = 1009,
  kInternalError = 1099,
  kPlatformOffline = 2001,
  kResourceOffline = 2002,
  kCatalogSyncFailed = 2003,
  kSipTransportFailed = 3001,
  kSipRequestRejected = 3002,
  kSdpNegotiationFailed = 3003,
  kMediaAllocationFailed = 4001,
  kMediaConnectFailed = 4002,
  kMediaReceiveTimeout = 4003,
  kUnsupportedMedia = 4004,
  kStorageBusy = 5001,
  kStorageReadFailed = 5002,
  kStorageWriteFailed = 5003,
  kStorageMigrationFailed = 5004,
};
}  // namespace gb28181
#endif  // GB28181_INCLUDE_GB28181_ERROR_CODE_H_
