#include "gb28181/error.h"

namespace gb28181 {
const char* ErrorMessage(ErrorCode code) {
  switch (code) {
    case ErrorCode::kOk:
      return "Success";
    case ErrorCode::kInvalidArgument:
      return "Invalid argument";
    case ErrorCode::kUnauthenticated:
      return "Authentication required";
    case ErrorCode::kPermissionDenied:
      return "Permission denied";
    case ErrorCode::kNotFound:
      return "Not found";
    case ErrorCode::kAlreadyExists:
      return "Already exists";
    case ErrorCode::kInvalidState:
      return "Invalid state";
    case ErrorCode::kCancelled:
      return "Cancelled";
    case ErrorCode::kTimeout:
      return "Request timed out";
    case ErrorCode::kResourceExhausted:
      return "Resource limit reached";
    case ErrorCode::kPlatformOffline:
      return "Platform offline";
    case ErrorCode::kResourceOffline:
      return "Resource offline";
    case ErrorCode::kCatalogSyncFailed:
      return "Catalog synchronization failed";
    case ErrorCode::kSipTransportFailed:
      return "SIP transport failed";
    case ErrorCode::kSipRequestRejected:
      return "SIP request rejected";
    case ErrorCode::kSdpNegotiationFailed:
      return "SDP negotiation failed";
    case ErrorCode::kMediaAllocationFailed:
      return "Media allocation failed";
    case ErrorCode::kMediaConnectFailed:
      return "Media connection failed";
    case ErrorCode::kMediaReceiveTimeout:
      return "Media reception timed out";
    case ErrorCode::kUnsupportedMedia:
      return "Unsupported media";
    case ErrorCode::kStorageBusy:
      return "Storage busy";
    case ErrorCode::kStorageReadFailed:
      return "Storage read failed";
    case ErrorCode::kStorageWriteFailed:
      return "Storage write failed";
    case ErrorCode::kStorageMigrationFailed:
      return "Storage migration failed";
    default:
      return "Internal error";
  }
}
}  // namespace gb28181
