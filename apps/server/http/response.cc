#include "apps/server/http/response.h"

#include <utility>

#include "gb28181/error.h"

namespace gb28181::http {
int HttpStatus(ErrorCode code) {
  switch (code) {
    case ErrorCode::kOk:
      return 200;
    case ErrorCode::kInvalidArgument:
      return 400;
    case ErrorCode::kUnauthenticated:
      return 401;
    case ErrorCode::kPermissionDenied:
      return 403;
    case ErrorCode::kNotFound:
      return 404;
    case ErrorCode::kAlreadyExists:
    case ErrorCode::kInvalidState:
    case ErrorCode::kCancelled:
      return 409;
    case ErrorCode::kUnsupportedMedia:
      return 422;
    case ErrorCode::kPlatformOffline:
    case ErrorCode::kResourceOffline:
    case ErrorCode::kResourceExhausted:
    case ErrorCode::kStorageBusy:
    case ErrorCode::kMediaAllocationFailed:
      return 503;
    case ErrorCode::kTimeout:
    case ErrorCode::kMediaReceiveTimeout:
      return 504;
    case ErrorCode::kSipTransportFailed:
    case ErrorCode::kSipRequestRejected:
    case ErrorCode::kSdpNegotiationFailed:
    case ErrorCode::kMediaConnectFailed:
    case ErrorCode::kCatalogSyncFailed:
      return 502;
    default:
      return 500;
  }
}

drogon::HttpResponsePtr Response(Json::Value data,
                                 int status,
                                 const std::string& trace) {
  Json::Value body;
  body["code"] = 0;
  body["message"] = "Success";
  body["data"] = std::move(data);
  body["trace_id"] = trace;
  auto response = drogon::HttpResponse::newHttpJsonResponse(body);
  response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
  response->addHeader("X-Request-ID", trace);
  response->addHeader("Cache-Control", "no-store");
  return response;
}

drogon::HttpResponsePtr FailureResponse(Error error) {
  Json::Value body;
  body["code"] = static_cast<Json::UInt>(error.code);
  body["message"] = PublicMessage(error);
  body["data"] = Json::Value();
  body["trace_id"] = error.trace_id;
  auto response = drogon::HttpResponse::newHttpJsonResponse(body);
  response->setStatusCode(
      static_cast<drogon::HttpStatusCode>(HttpStatus(error.code)));
  response->addHeader("X-Request-ID", error.trace_id);
  response->addHeader("Cache-Control", "no-store");
  if (error.code == ErrorCode::kUnauthenticated)
    response->addHeader("WWW-Authenticate", "Bearer");
  return response;
}
}  // namespace gb28181::http
