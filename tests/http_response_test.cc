#include "apps/server/http/response.h"
#include "tests/test_support.h"

int main() {
  using namespace gb28181;
  using test::Require;
  Require(http::HttpStatus(ErrorCode::kInvalidArgument) == 400 &&
              http::HttpStatus(ErrorCode::kUnauthenticated) == 401 &&
              http::HttpStatus(ErrorCode::kPermissionDenied) == 403 &&
              http::HttpStatus(ErrorCode::kMediaConnectFailed) == 502 &&
              http::HttpStatus(ErrorCode::kMediaAllocationFailed) == 503 &&
              http::HttpStatus(ErrorCode::kMediaReceiveTimeout) == 504,
          "HTTP status preserves error meaning");
  auto failure = http::FailureResponse(
      {ErrorCode::kStorageReadFailed, "SELECT password FROM C:/private/data.db",
       "trace_a"});
  const auto body = failure->getJsonObject();
  Require(
      failure->statusCode() == 500 && body &&
          (*body)["code"].asUInt() == 5002 &&
          (*body)["trace_id"] == "trace_a" && (*body)["data"].isNull() &&
          failure->body().find("private") == std::string_view::npos &&
          failure->body().find("SELECT") == std::string_view::npos,
      "failure response preserves code and trace while redacting internals");
  Playback playback;
  playback.error = {ErrorCode::kInternalError, "secret SQL path", "trace_b"};
  Require(http::Serialize(playback)["error"]["message"].asString().find(
              "secret") == std::string::npos,
          "asynchronous playback error also redacted");
  Platform platform;
  platform.config.password = "never-return-this";
  const auto serialized = http::Serialize(platform);
  Require(!serialized.isMember("password") && !serialized.isMember("config"),
          "platform DTO never returns credentials");
}
