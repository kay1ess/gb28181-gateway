#include "apps/server/http/response.h"

#include <utility>

#include "gb28181/error.h"

namespace gb28181::http {
namespace {
const char* State(OnlineState value) {
  switch (value) {
    case OnlineState::kOnline:
      return "online";
    case OnlineState::kOffline:
      return "offline";
    default:
      return "unknown";
  }
}

std::string PublicMessage(const Error& error) {
  switch (error.code) {
    case ErrorCode::kInternalError:
    case ErrorCode::kStorageReadFailed:
    case ErrorCode::kStorageWriteFailed:
    case ErrorCode::kStorageMigrationFailed:
      return ErrorMessage(error.code);
    default:
      return error.message.empty() ? ErrorMessage(error.code) : error.message;
  }
}

Json::Value ErrorValue(const Error& error) {
  Json::Value value;
  value["code"] = static_cast<Json::UInt>(error.code);
  value["message"] = PublicMessage(error);
  value["trace_id"] = error.trace_id;
  return value;
}
}  // namespace

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

Json::Value Serialize(const Platform& value) {
  Json::Value result;
  const auto& config = value.config;
  result["id"] = config.id;
  result["gb_id"] = config.gb_id;
  result["name"] = config.name;
  result["realm"] = config.realm;
  result["username"] = config.username;
  result["enabled"] = config.enabled;
  result["default_receive_mode"] =
      config.default_receive_mode == RtpReceiveMode::kUdp ? "udp"
      : config.default_receive_mode == RtpReceiveMode::kTcpPassive
          ? "tcp_passive"
          : "tcp_active";
  result["heartbeat_timeout_seconds"] = config.heartbeat_timeout_seconds;
  result["state"] = State(value.state);
  result["last_seen_ms"] = Json::Int64(value.last_seen_ms);
  result["remote_address"] = value.remote_address;
  result["remote_port"] = value.remote_port;
  result["sip_transport"] = value.sip_transport;
  // Never serialize the authentication password, including on create/update.
  return result;
}

Json::Value Serialize(const Resource& value) {
  Json::Value result;
  result["id"] = value.id;
  result["platform_id"] = value.platform_id;
  result["gb_id"] = value.gb_id;
  result["parent_gb_id"] = value.parent_gb_id;
  result["name"] = value.name;
  result["manufacturer"] = value.manufacturer;
  result["model"] = value.model;
  result["address"] = value.address;
  result["civil_code"] = value.civil_code;
  result["parental"] = value.parental;
  result["state"] = State(value.state);
  result["updated_ms"] = Json::Int64(value.updated_ms);
  return result;
}

Json::Value Serialize(const Operation& value) {
  Json::Value result;
  result["id"] = value.id;
  result["platform_id"] = value.platform_id;
  const char* state = "running";
  if (value.state == OperationState::kSucceeded)
    state = "succeeded";
  else if (value.state == OperationState::kFailed)
    state = "failed";
  else if (value.state == OperationState::kCancelled)
    state = "cancelled";
  result["state"] = state;
  result["received"] = value.received;
  result["expected"] = value.expected;
  result["error"] = ErrorValue(value.error);
  return result;
}

Json::Value Serialize(const DeviceInfo& value) {
  Json::Value result;
  result["resource_id"] = value.resource_id;
  result["name"] = value.name;
  result["manufacturer"] = value.manufacturer;
  result["model"] = value.model;
  result["firmware"] = value.firmware;
  result["queried_ms"] = Json::Int64(value.queried_ms);
  return result;
}

Json::Value Serialize(const DeviceStatus& value) {
  Json::Value result;
  result["resource_id"] = value.resource_id;
  result["state"] = State(value.state);
  result["status"] = value.status;
  result["queried_ms"] = Json::Int64(value.queried_ms);
  return result;
}

Json::Value Serialize(const Playback& value) {
  Json::Value result;
  result["id"] = value.id;
  result["resource_id"] = value.resource_id;
  const char* state = "negotiating";
  switch (value.state) {
    case PlaybackState::kWaitingMedia:
      state = "waiting_media";
      break;
    case PlaybackState::kPlaying:
      state = "playing";
      break;
    case PlaybackState::kStopping:
      state = "stopping";
      break;
    case PlaybackState::kEnded:
      state = "ended";
      break;
    case PlaybackState::kFailed:
      state = "failed";
      break;
    default:
      break;
  }
  result["state"] = state;
  result["mode"] = value.mode == RtpReceiveMode::kUdp          ? "udp"
                   : value.mode == RtpReceiveMode::kTcpPassive ? "tcp_passive"
                                                               : "tcp_active";
  result["ssrc"] = value.ssrc;
  result["consumers"] = value.consumers;
  result["created_ms"] = Json::Int64(value.created_ms);
  result["negotiated_ms"] = Json::Int64(value.negotiated_ms);
  result["media_ready_ms"] = Json::Int64(value.media_ready_ms);
  result["ready_ms"] = Json::Int64(value.ready_ms);
  result["error"] = ErrorValue(value.error);
  result["endpoints"] = Json::Value(Json::arrayValue);
  for (const auto& endpoint : value.endpoints) {
    Json::Value item;
    item["protocol"] = endpoint.protocol;
    item["url"] = endpoint.url;
    result["endpoints"].append(std::move(item));
  }
  return result;
}

Json::Value Serialize(const PlaybackLease& value) {
  Json::Value result;
  result["id"] = value.id;
  result["playback_id"] = value.playback_id;
  result["expires_ms"] = Json::Int64(value.expires_ms);
  return result;
}

Json::Value Serialize(const Statistics& value) {
  Json::Value result;
  result["online_platforms"] = value.online_platforms;
  result["active_queries"] = value.active_queries;
  result["active_playbacks"] = value.active_playbacks;
  result["active_receivers"] = value.active_receivers;
  result["playback_leases"] = value.playback_leases;
  result["sip_sessions"] = value.sip_sessions;
  result["sip_requests"] = value.sip_requests;
  return result;
}

Json::Value Serialize(const Event& value) {
  Json::Value result;
  const char* type = "platform_changed";
  if (value.type == EventType::kCatalogChanged)
    type = "catalog_changed";
  else if (value.type == EventType::kOperationChanged)
    type = "operation_changed";
  else if (value.type == EventType::kPlaybackChanged)
    type = "playback_changed";
  result["type"] = type;
  result["entity_id"] = value.entity_id;
  result["trace_id"] = value.trace_id;
  result["error"] = ErrorValue(value.error);
  if (value.operation)
    result["operation"] = Serialize(*value.operation);
  return result;
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
