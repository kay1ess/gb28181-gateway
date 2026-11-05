#include "apps/server/http/server.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <utility>

#include <drogon/WebSocketController.h>
#include <drogon/drogon.h>
#include <trantor/net/EventLoop.h>

#include "apps/server/http/response.h"

namespace gb28181::http {
namespace {
std::string Trace(const drogon::HttpRequestPtr& request) {
  auto trace = request->getHeader("x-request-id");
  if (!trace.empty() && trace.size() <= 64 &&
      std::all_of(trace.begin(), trace.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
      }))
    return trace;
  static std::atomic<uint64_t> sequence{0};
  return "http_" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()) +
         "_" + std::to_string(++sequence);
}

Error Failure(ErrorCode code, const std::string& trace) {
  return {code, ErrorMessage(code), trace};
}

RequestOptions Options(const std::string& trace) {
  RequestOptions options;
  options.trace_id = trace;
  return options;
}

bool Authorized(const std::string& supplied, const std::string& token) {
  const std::string expected = "Bearer " + token;
  if (supplied.size() != expected.size())
    return false;
  unsigned char difference = 0;
  for (size_t index = 0; index < expected.size(); ++index)
    difference |= static_cast<unsigned char>(supplied[index] ^ expected[index]);
  return difference == 0;
}

class BearerFilter final : public drogon::HttpFilter<BearerFilter, false> {
 public:
  explicit BearerFilter(std::string token) : token_(std::move(token)) {
  }

  void doFilter(const drogon::HttpRequestPtr& request,
                drogon::FilterCallback&& reject,
                drogon::FilterChainCallback&& next) override {
    if (!Authorized(request->getHeader("authorization"), token_))
      reject(FailureResponse(
          Failure(ErrorCode::kUnauthenticated, Trace(request))));
    else
      next();
  }

 private:
  std::string token_;
};

class EventSocket final
    : public drogon::WebSocketController<EventSocket, false> {
 public:
  EventSocket(std::shared_ptr<Client> client, std::string token)
      : client_(std::move(client)), token_(std::move(token)) {
  }

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/api/events");

  WS_PATH_LIST_END
  void handleNewConnection(
      const drogon::HttpRequestPtr&,
      const drogon::WebSocketConnectionPtr& connection) override {
    auto state = std::make_shared<ConnectionState>();
    connection->setContext(state);
    std::weak_ptr<drogon::WebSocketConnection> weak = connection;
    state->auth_timer =
        trantor::EventLoop::getEventLoopOfCurrentThread()->runAfter(
            kAuthTimeout, [weak] {
              auto socket = weak.lock();
              if (!socket)
                return;
              auto state = socket->getContext<ConnectionState>();
              if (!state)
                return;
              state->auth_timer = 0;
              if (!state->subscription && !state->closing)
                Close(socket, state, drogon::CloseCode::kViolation,
                      "Authentication timed out");
            });
  }

  void handleConnectionClosed(
      const drogon::WebSocketConnectionPtr& connection) override {
    const auto state = connection->getContext<ConnectionState>();
    if (!state)
      return;
    state->ready.store(false);
    CancelAuthTimer(*state);
    if (state->subscription)
      client_->UnsubscribeEvents(*state->subscription);
    connection->clearContext();
  }

  void handleNewMessage(const drogon::WebSocketConnectionPtr& connection,
                        std::string&& message,
                        const drogon::WebSocketMessageType& type) override {
    if (type != drogon::WebSocketMessageType::Text &&
        type != drogon::WebSocketMessageType::Binary)
      return;
    const auto state = connection->getContext<ConnectionState>();
    if (!state || state->closing)
      return;
    if (state->subscription) {
      if (type == drogon::WebSocketMessageType::Text && message == "ping")
        connection->send("pong");
      else if (type == drogon::WebSocketMessageType::Text)
        HandleCommand(connection, ParseMessage(message));
      return;
    }
    if (std::chrono::steady_clock::now() >= state->deadline) {
      Close(connection, state, drogon::CloseCode::kViolation,
            "Authentication timed out");
      return;
    }
    if (type != drogon::WebSocketMessageType::Text ||
        !ValidAuthMessage(message)) {
      Close(connection, state, drogon::CloseCode::kViolation,
            "Authentication failed");
      return;
    }

    std::weak_ptr<drogon::WebSocketConnection> weak = connection;
    std::weak_ptr<ConnectionState> weak_state = state;
    auto subscription =
        client_->SubscribeEvents([weak, weak_state](Event event) {
          const auto state = weak_state.lock();
          if (!state || !state->ready.load())
            return;
          if (auto socket = weak.lock()) {
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            socket->send(Json::writeString(writer, Serialize(event)));
          }
        });
    if (!subscription) {
      Close(connection, state, drogon::CloseCode::kUnexpectedCondition,
            "SDK unavailable");
      return;
    }
    state->subscription = subscription.value();
    CancelAuthTimer(*state);
    connection->send(R"({"type":"auth_ok"})");
    state->ready.store(true);
  }

 private:
  static constexpr auto kAuthTimeout = std::chrono::seconds(5);

  struct ConnectionState {
    // Only the SDK event callback reads ready outside the connection loop.
    std::atomic<bool> ready{false};
    std::optional<uint64_t> subscription;
    trantor::TimerId auth_timer = 0;
    bool closing = false;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + kAuthTimeout;
  };

  static void CancelAuthTimer(ConnectionState& state) {
    if (state.auth_timer) {
      trantor::EventLoop::getEventLoopOfCurrentThread()->invalidateTimer(
          state.auth_timer);
      state.auth_timer = 0;
    }
  }

  static void Close(const drogon::WebSocketConnectionPtr& connection,
                    const std::shared_ptr<ConnectionState>& state,
                    drogon::CloseCode code,
                    const char* reason) {
    state->closing = true;
    CancelAuthTimer(*state);
    connection->shutdown(code, reason);
    // Bound the lifetime even if the peer ignores the closing handshake.
    std::weak_ptr<drogon::WebSocketConnection> weak = connection;
    trantor::EventLoop::getEventLoopOfCurrentThread()->runAfter(1.0, [weak] {
      if (auto socket = weak.lock())
        socket->forceClose();
    });
  }

  static Json::Value ParseMessage(const std::string& message) {
    if (message.size() > 4096)
      return {};
    Json::CharReaderBuilder builder;
    Json::CharReaderBuilder::strictMode(&builder.settings_);
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value value;
    if (reader->parse(message.data(), message.data() + message.size(), &value,
                      nullptr) &&
        value.isObject())
      return value;
    return {};
  }

  bool ValidAuthMessage(const std::string& message) const {
    const auto auth = ParseMessage(message);
    return auth["type"] == "auth" && auth["token"].isString() &&
           Authorized("Bearer " + auth["token"].asString(), token_);
  }

  void HandleCommand(const drogon::WebSocketConnectionPtr& connection,
                     const Json::Value& command) {
    const auto request_id = command["request_id"].isString()
                                ? command["request_id"].asString()
                                : std::string();
    std::weak_ptr<drogon::WebSocketConnection> weak = connection;
    auto reply = [weak, request_id](Result<Operation> result) {
      auto socket = weak.lock();
      if (!socket)
        return;
      Json::Value response;
      response["type"] = result ? "catalog_accepted" : "catalog_error";
      response["request_id"] = request_id.size() <= 64 ? request_id : "";
      if (result) {
        response["operation"] = Serialize(result.value());
      } else {
        response["error"]["code"] =
            static_cast<Json::UInt>(result.error().code);
        response["error"]["message"] = ErrorMessage(result.error().code);
      }
      Json::StreamWriterBuilder writer;
      writer["indentation"] = "";
      socket->send(Json::writeString(writer, response));
    };
    const auto platform_id = command["platform_id"].isString()
                                 ? command["platform_id"].asString()
                                 : std::string();
    if (command["type"] != "catalog_sync" || request_id.empty() ||
        request_id.size() > 64 || platform_id.empty() ||
        platform_id.size() > 64) {
      reply(Failure(ErrorCode::kInvalidArgument, request_id));
      return;
    }
    auto accepted =
        client_->SyncCatalog(platform_id, reply, Options(request_id));
    if (!accepted)
      reply(accepted.error());
  }

  std::shared_ptr<Client> client_;
  const std::string token_;
};

Json::Value Body(const drogon::HttpRequestPtr& request) {
  auto body = request->getJsonObject();
  if (!body || !body->isObject())
    throw std::invalid_argument("expected JSON object");
  return *body;
}

std::string Text(const Json::Value& body,
                 const char* field,
                 const std::string& fallback = {}) {
  if (!body.isMember(field))
    return fallback;
  if (!body[field].isString())
    throw std::invalid_argument("expected string");
  auto result = body[field].asString();
  if (result.size() > 4096)
    throw std::invalid_argument("field too long");
  return result;
}

RtpReceiveMode ReceiveMode(const std::string& mode) {
  if (mode == "udp")
    return RtpReceiveMode::kUdp;
  if (mode == "tcp_passive")
    return RtpReceiveMode::kTcpPassive;
  if (mode == "tcp_active")
    return RtpReceiveMode::kTcpActive;
  throw std::invalid_argument("invalid RTP mode");
}

PlatformConfig PlatformBody(const Json::Value& body,
                            PlatformConfig platform = {}) {
  platform.id = Text(body, "id", platform.id);
  platform.gb_id = Text(body, "gb_id", platform.gb_id);
  platform.name = Text(body, "name", platform.name);
  platform.realm = Text(body, "realm", platform.realm);
  platform.username = Text(body, "username", platform.username);
  platform.password = Text(body, "password", platform.password);
  if (body.isMember("default_receive_mode"))
    platform.default_receive_mode =
        ReceiveMode(Text(body, "default_receive_mode"));
  if (body.isMember("enabled")) {
    if (!body["enabled"].isBool())
      throw std::invalid_argument("expected enabled boolean");
    platform.enabled = body["enabled"].asBool();
  }
  if (body.isMember("heartbeat_timeout_seconds")) {
    if (!body["heartbeat_timeout_seconds"].isUInt())
      throw std::invalid_argument("invalid heartbeat timeout");
    platform.heartbeat_timeout_seconds =
        body["heartbeat_timeout_seconds"].asUInt();
  }
  return platform;
}

uint32_t Number(const std::string& value, uint32_t fallback) {
  if (value.empty())
    return fallback;
  uint32_t result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size())
    throw std::invalid_argument("invalid number");
  return result;
}

template <typename Work>
auto Guard(Work work) {
  return [work = std::move(work)](const drogon::HttpRequestPtr& request,
                                  ResponseCallback&& callback,
                                  const std::string& id) {
    const auto trace = Trace(request);
    auto once = std::make_shared<std::atomic<bool>>(false);
    ResponseCallback reply = [callback = std::move(callback),
                              once](const drogon::HttpResponsePtr& response) {
      if (!once->exchange(true))
        callback(response);
    };
    try {
      work(request, reply, id, trace);
    } catch (const std::invalid_argument&) {
      reply(FailureResponse(Failure(ErrorCode::kInvalidArgument, trace)));
    } catch (...) {
      reply(FailureResponse(Failure(ErrorCode::kInternalError, trace)));
    }
  };
}

template <typename Work>
auto GuardRoot(Work work) {
  return
      [handler = Guard(std::move(work))](const drogon::HttpRequestPtr& request,
                                         ResponseCallback&& callback) {
        handler(request, std::move(callback), "");
      };
}

template <typename T, typename Invoke>
void InvokeSdk(ResponseCallback callback,
               const std::string& trace,
               int status,
               Invoke invoke) {
  auto completion = Complete<T>(callback, trace, status);
  auto accepted = invoke(completion, Options(trace));
  if (!accepted) {
    auto error = accepted.error();
    error.trace_id = trace;
    callback(FailureResponse(std::move(error)));
  }
}
}  // namespace

void RegisterRoutes(std::shared_ptr<Client> client, const std::string& token) {
  if (token.size() < 16 || token.size() > 256)
    throw std::invalid_argument("api_token must contain 16 to 256 characters");
  auto& app = drogon::app();
  app.registerFilter(std::make_shared<BearerFilter>(token));
  app.registerController(std::make_shared<EventSocket>(client, token));
  const auto filter = BearerFilter::classTypeName();
  app.setCustomErrorHandler([](drogon::HttpStatusCode status,
                               const drogon::HttpRequestPtr& request) {
    auto error = Failure(status == drogon::k404NotFound ? ErrorCode::kNotFound
                         : status == drogon::k405MethodNotAllowed
                             ? ErrorCode::kInvalidArgument
                             : ErrorCode::kInternalError,
                         Trace(request));
    auto response = FailureResponse(error);
    response->setStatusCode(status);
    return response;
  });
  app.registerHandler(
      "/api/platforms",
      GuardRoot([client](const auto& request, auto reply, const auto&,
                         const auto& trace) {
        if (request->getMethod() == drogon::Get)
          InvokeSdk<std::vector<Platform>>(
              reply, trace, 200, [&](auto done, auto options) {
                return client->ListPlatforms(done, options);
              });
        else {
          auto platform = PlatformBody(Body(request));
          InvokeSdk<Platform>(reply, trace, 201, [&](auto done, auto options) {
            return client->AddPlatform(platform, done, options);
          });
        }
      }),
      {drogon::Get, drogon::Post, filter});
  app.registerHandler(
      "/api/platforms/{id}",
      Guard([client](const auto& request, auto reply, const auto& id,
                     const auto& trace) {
        if (request->getMethod() == drogon::Get)
          InvokeSdk<Platform>(reply, trace, 200, [&](auto done, auto options) {
            return client->GetPlatform(id, done, options);
          });
        else if (request->getMethod() == drogon::Delete)
          InvokeSdk<void>(reply, trace, 200, [&](auto done, auto options) {
            return client->RemovePlatform(id, done, options);
          });
        else {
          auto body = Body(request);
          if (body.isMember("id") && Text(body, "id") != id)
            throw std::invalid_argument("identity cannot change");
          auto accepted = client->GetPlatform(
              id,
              [client, body, reply, trace](Result<Platform> current) {
                if (!current) {
                  auto error = current.error();
                  error.trace_id = trace;
                  reply(FailureResponse(std::move(error)));
                  return;
                }
                try {
                  const auto platform =
                      PlatformBody(body, current.value().config);
                  InvokeSdk<Platform>(
                      reply, trace, 200, [&](auto done, auto options) {
                        return client->UpdatePlatform(platform, done, options);
                      });
                } catch (...) {
                  reply(FailureResponse(
                      Failure(ErrorCode::kInvalidArgument, trace)));
                }
              },
              Options(trace));
          if (!accepted)
            reply(FailureResponse(Failure(accepted.error().code, trace)));
        }
      }),
      {drogon::Get, drogon::Patch, drogon::Delete, filter});
  app.registerHandler("/api/operations/{id}",
                      Guard([client](const auto&, auto reply, const auto& id,
                                     const auto& trace) {
                        InvokeSdk<Operation>(
                            reply, trace, 200, [&](auto done, auto options) {
                              return client->GetOperation(id, done, options);
                            });
                      }),
                      {drogon::Get, filter});
  app.registerHandler(
      "/api/resources",
      GuardRoot([client](const auto& request, auto reply, const auto&,
                         const auto& trace) {
        ResourceFilter values;
        values.platform_id = request->getParameter("platform_id");
        values.keyword = request->getParameter("keyword");
        values.offset = Number(request->getParameter("offset"), 0);
        values.limit = Number(request->getParameter("limit"), 100);
        const auto state = request->getParameter("state");
        if (state == "online")
          values.state = OnlineState::kOnline;
        else if (state == "offline")
          values.state = OnlineState::kOffline;
        else if (state == "unknown")
          values.state = OnlineState::kUnknown;
        else if (!state.empty())
          throw std::invalid_argument("invalid resource state");
        InvokeSdk<std::vector<Resource>>(
            reply, trace, 200, [&](auto done, auto options) {
              return client->ListResources(values, done, options);
            });
      }),
      {drogon::Get, filter});
  app.registerHandler("/api/resources/{id}",
                      Guard([client](const auto&, auto reply, const auto& id,
                                     const auto& trace) {
                        InvokeSdk<Resource>(
                            reply, trace, 200, [&](auto done, auto options) {
                              return client->GetResource(id, done, options);
                            });
                      }),
                      {drogon::Get, filter});
  app.registerHandler(
      "/api/resources/{id}/info-refresh",
      Guard([client](const auto&, auto reply, const auto& id,
                     const auto& trace) {
        InvokeSdk<DeviceInfo>(reply, trace, 200, [&](auto done, auto options) {
          return client->RefreshDeviceInfo(id, done, options);
        });
      }),
      {drogon::Post, filter});
  app.registerHandler("/api/resources/{id}/info",
                      Guard([client](const auto&, auto reply, const auto& id,
                                     const auto& trace) {
                        InvokeSdk<DeviceInfo>(
                            reply, trace, 200, [&](auto done, auto options) {
                              return client->GetDeviceInfo(id, done, options);
                            });
                      }),
                      {drogon::Get, filter});
  app.registerHandler("/api/resources/{id}/status",
                      Guard([client](const auto&, auto reply, const auto& id,
                                     const auto& trace) {
                        InvokeSdk<DeviceStatus>(
                            reply, trace, 200, [&](auto done, auto options) {
                              return client->GetDeviceStatus(id, done, options);
                            });
                      }),
                      {drogon::Get, filter});
  app.registerHandler(
      "/api/resources/{id}/status-refresh",
      Guard(
          [client](const auto&, auto reply, const auto& id, const auto& trace) {
            InvokeSdk<DeviceStatus>(
                reply, trace, 200, [&](auto done, auto options) {
                  return client->RefreshDeviceStatus(id, done, options);
                });
          }),
      {drogon::Post, filter});
  app.registerHandler("/api/playbacks",
                      GuardRoot([client](const auto& request, auto reply,
                                         const auto&, const auto& trace) {
                        auto body = Body(request);
                        StartLiveRequest live;
                        live.resource_id = Text(body, "resource_id");
                        if (body.isMember("mode"))
                          live.mode = ReceiveMode(Text(body, "mode"));
                        InvokeSdk<PlaybackLease>(
                            reply, trace, 202, [&](auto done, auto options) {
                              return client->StartLive(live, done, options);
                            });
                      }),
                      {drogon::Post, filter});
  app.registerHandler("/api/playbacks/{id}",
                      Guard([client](const auto&, auto reply, const auto& id,
                                     const auto& trace) {
                        InvokeSdk<Playback>(
                            reply, trace, 200, [&](auto done, auto options) {
                              return client->GetPlayback(id, done, options);
                            });
                      }),
                      {drogon::Get, filter});
  app.registerHandler(
      "/api/playbacks/{id}/stop",
      Guard(
          [client](const auto&, auto reply, const auto& id, const auto& trace) {
            InvokeSdk<void>(reply, trace, 200, [&](auto done, auto options) {
              return client->ForceStopPlayback(id, done, options);
            });
          }),
      {drogon::Post, filter});
  app.registerHandler(
      "/api/playback-leases/{id}",
      Guard(
          [client](const auto&, auto reply, const auto& id, const auto& trace) {
            InvokeSdk<void>(reply, trace, 200, [&](auto done, auto options) {
              return client->ReleasePlayback(id, done, options);
            });
          }),
      {drogon::Delete, filter});
  app.registerHandler("/api/playback-leases/{id}/renew",
                      Guard([client](const auto&, auto reply, const auto& id,
                                     const auto& trace) {
                        InvokeSdk<PlaybackLease>(
                            reply, trace, 200, [&](auto done, auto options) {
                              return client->RenewPlayback(id, done, options);
                            });
                      }),
                      {drogon::Post, filter});
  app.registerHandler("/api/health",
                      GuardRoot([client](const auto&, auto reply, const auto&,
                                         const auto& trace) {
                        InvokeSdk<Statistics>(
                            reply, trace, 200, [&](auto done, auto options) {
                              return client->GetStatistics(done, options);
                            });
                      }),
                      {drogon::Get, filter});
}
}  // namespace gb28181::http
