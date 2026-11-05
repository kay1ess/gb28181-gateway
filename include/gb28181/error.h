#ifndef GB28181_INCLUDE_GB28181_ERROR_H_
#define GB28181_INCLUDE_GB28181_ERROR_H_
#include <string>

#include "gb28181/error_code.h"

namespace gb28181 {
struct Error {
  ErrorCode code = ErrorCode::kInternalError;
  std::string message;
  std::string trace_id;
};

const char* ErrorMessage(ErrorCode code);
}  // namespace gb28181
#endif  // GB28181_INCLUDE_GB28181_ERROR_H_
