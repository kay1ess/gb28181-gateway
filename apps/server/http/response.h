#ifndef GB28181_APPS_SERVER_HTTP_RESPONSE_H_
#define GB28181_APPS_SERVER_HTTP_RESPONSE_H_
#include <functional>
#include <string>

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "gb28181/result.h"
#include "src/core/sdk_json.h"

namespace gb28181::http {
using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;
int HttpStatus(ErrorCode code);
using gb28181::Serialize;

drogon::HttpResponsePtr Response(Json::Value data,
                                 int status,
                                 const std::string& trace);
drogon::HttpResponsePtr FailureResponse(Error error);

template <typename T>
std::function<void(Result<T>)> Complete(ResponseCallback callback,
                                        const std::string& trace,
                                        int status = 200) {
  return [callback = std::move(callback), trace, status](Result<T> result) {
    if (!result) {
      auto error = result.error();
      error.trace_id = trace;
      callback(FailureResponse(std::move(error)));
    } else
      callback(Response(Serialize(result.value()), status, trace));
  };
}

template <>
inline std::function<void(Result<void>)> Complete<void>(
    ResponseCallback callback,
    const std::string& trace,
    int status) {
  return [callback = std::move(callback), trace, status](Result<void> result) {
    if (!result) {
      auto error = result.error();
      error.trace_id = trace;
      callback(FailureResponse(std::move(error)));
    } else
      callback(Response(Json::Value(), status, trace));
  };
}
}  // namespace gb28181::http
#endif  // GB28181_APPS_SERVER_HTTP_RESPONSE_H_
