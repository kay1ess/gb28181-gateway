#ifndef GB28181_APPS_SERVER_HTTP_RESPONSE_H_
#define GB28181_APPS_SERVER_HTTP_RESPONSE_H_
#include <functional>
#include <string>
#include <vector>

#include <drogon/HttpResponse.h>
#include <json/json.h>

#include "gb28181/events.h"
#include "gb28181/result.h"
#include "gb28181/types.h"

namespace gb28181::http {
using ResponseCallback = std::function<void(const drogon::HttpResponsePtr&)>;
int HttpStatus(ErrorCode code);
Json::Value Serialize(const Platform& value);
Json::Value Serialize(const Resource& value);
Json::Value Serialize(const Operation& value);
Json::Value Serialize(const DeviceInfo& value);
Json::Value Serialize(const DeviceStatus& value);
Json::Value Serialize(const Playback& value);
Json::Value Serialize(const PlaybackLease& value);
Json::Value Serialize(const Statistics& value);
Json::Value Serialize(const Event& value);

template <typename T>
Json::Value Serialize(const std::vector<T>& values) {
  Json::Value result(Json::arrayValue);
  for (const auto& value : values)
    result.append(Serialize(value));
  return result;
}

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
