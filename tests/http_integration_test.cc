#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <drogon/HttpClient.h>
#include <drogon/WebSocketClient.h>
#include <trantor/net/EventLoopThread.h>

#include "tests/child_process.h"
#include "tests/udp_peer.h"

namespace {
using namespace std::chrono_literals;
const std::string kToken = "integration-only-token";

void Check(bool value, const std::string& message) {
  if (!value)
    throw std::runtime_error(message);
}

const char* Environment(const char* name) {
  const auto value = std::getenv(name);
  Check(value && *value, std::string("missing environment: ") + name);
  return value;
}

class Http {
 public:
  Http(uint16_t port, trantor::EventLoop* loop)
      : client_(
            drogon::HttpClient::newHttpClient("127.0.0.1", port, false, loop)) {
  }

  Json::Value Request(drogon::HttpMethod method,
                      const std::string& path,
                      Json::Value body = {},
                      int expected = 200,
                      bool authenticate = true) {
    auto request = body.isNull()
                       ? drogon::HttpRequest::newHttpRequest()
                       : drogon::HttpRequest::newHttpJsonRequest(body);
    request->setMethod(method);
    request->setPath(path);
    request->addHeader("X-Request-ID", "http_integration");
    if (authenticate)
      request->addHeader("Authorization", "Bearer " + kToken);
    auto [result, response] = client_->sendRequest(request, 12);
    Check(result == drogon::ReqResult::Ok && response,
          "HTTP transport failure: " + path);
    Check(response->statusCode() == expected,
          path + " expected HTTP " + std::to_string(expected) + " got " +
              std::to_string(response->statusCode()) + " " +
              std::string(response->body()));
    const auto json = response->getJsonObject();
    Check(json && json->isMember("code") && json->isMember("data") &&
              (*json)["trace_id"] == "http_integration" &&
              response->getHeader("x-request-id") == "http_integration",
          "unified response and trace: " + path);
    Check(response->body().find("test-only") == std::string_view::npos,
          "password leaked in HTTP response");
    return *json;
  }

 private:
  drogon::HttpClientPtr client_;
};

template <typename Predicate>
void Until(Predicate predicate, const std::string& message) {
  const auto deadline = std::chrono::steady_clock::now() + 20s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate())
      return;
    std::this_thread::sleep_for(30ms);
  }
  throw std::runtime_error(message);
}

struct Events {
  std::mutex mutex;
  std::condition_variable changed;
  bool catalog = false;
  bool playback = false;
  bool rejected_playback = false;
  bool authenticated = false;
  bool pong = false;
  std::string catalog_progress_id;
  std::string catalog_completed_id;
  std::string catalog_request_id;
  std::string catalog_accepted_id;
  std::string catalog_error;
};

void CheckWebSocketRejection(uint16_t port,
                             trantor::EventLoop* loop,
                             Http& http,
                             bool send_bad_token) {
  auto socket = drogon::WebSocketClient::newWebSocketClient("127.0.0.1", port,
                                                            false, loop);
  auto closed = std::make_shared<std::promise<std::string>>();
  auto close = closed->get_future();
  auto received_text = std::make_shared<std::atomic<bool>>(false);
  socket->setMessageHandler(
      [closed, received_text](std::string&& message, const auto&,
                              drogon::WebSocketMessageType type) {
        if (type == drogon::WebSocketMessageType::Text)
          received_text->store(true);
        else if (type == drogon::WebSocketMessageType::Close)
          closed->set_value(std::move(message));
      });
  auto upgraded = std::make_shared<std::promise<bool>>();
  auto upgrade = upgraded->get_future();
  auto request = drogon::HttpRequest::newHttpRequest();
  request->setPath("/api/events");
  socket->connectToServer(
      request, [upgraded, send_bad_token](auto result, const auto& response,
                                          const auto& client) {
        const bool success = result == drogon::ReqResult::Ok && response &&
                             response->statusCode() == 101;
        if (success && send_bad_token)
          client->getConnection()->send(R"({"type":"auth","token":"wrong"})");
        upgraded->set_value(success);
      });
  Check(upgrade.wait_for(5s) == std::future_status::ready && upgrade.get(),
        "WebSocket upgrade without authorization header");
  if (!send_bad_token)
    http.Request(drogon::Patch, "/api/platforms/lower",
                 Json::Value(Json::objectValue));
  Check(close.wait_for(send_bad_token ? 2s : 7s) == std::future_status::ready,
        "unauthenticated WebSocket closes");
  const auto frame = close.get();
  Check(frame.size() >= 2 && static_cast<unsigned char>(frame[0]) == 3 &&
            static_cast<unsigned char>(frame[1]) == 240 &&
            frame.substr(2) == (send_bad_token ? "Authentication failed"
                                               : "Authentication timed out") &&
            !received_text->load(),
        "policy close without leaking events");
  socket->stop();
}

void Test(const std::string& transport,
          const std::filesystem::path& directory) {
  const bool independent = std::getenv("GB_PEER_KIND") &&
                           std::string(std::getenv("GB_PEER_KIND")) == "mickey";
  const auto http_port = gb28181::test::UnusedSipPort();
  auto sip_port = gb28181::test::UnusedSipPort();
  while (sip_port == http_port)
    sip_port = gb28181::test::UnusedSipPort();
  Json::Value config;
  config["database_path"] = (directory / (transport + ".db")).string();
  config["bind_address"] = "127.0.0.1";
  config["http_port"] = http_port;
  config["sip_port"] = sip_port;
  config["media_http_port"] = 0;
  config["media_rtsp_port"] = 0;
  config["rtp_port"] = 0;
  config["api_token"] = kToken;
  const auto config_path = directory / (transport + ".json");
  {
    std::ofstream file(config_path);
    file << config;
  }
  gb28181::test::ChildProcess server(
      {Environment("GB_SERVER"), config_path.string()});
  trantor::EventLoopThread thread;
  thread.run();
  Http http(http_port, thread.getLoop());
  Until(
      [&] {
        try {
          return http.Request(drogon::Get, "/api/health")["code"] == 0;
        } catch (...) {
          return false;
        }
      },
      "HTTP service did not start");
  Check(http.Request(drogon::Get, "/api/platforms", {}, 401, false)["code"] ==
            1002,
        "unauthenticated error code");
  Check(http.Request(drogon::Get, "/api/platforms/missing", {}, 404)["code"] ==
            1004,
        "not-found SDK error mapping");
  Check(http.Request(drogon::Get, "/api/resources?limit=invalid", {},
                     400)["code"] == 1001,
        "invalid pagination parameter");
  Json::Value platform;
  platform["id"] = "lower";
  platform["gb_id"] = "34020000002000000002";
  platform["password"] = "test-only";
  platform["heartbeat_timeout_seconds"] = 2;
  http.Request(drogon::Post, "/api/platforms", platform, 201);
  Check(http.Request(drogon::Post, "/api/platforms", platform, 409)["code"] ==
            1005,
        "duplicate SDK error mapping");

  auto generated_body = platform;
  generated_body.removeMember("id");
  generated_body["gb_id"] = "34020000002000000003";
  const auto generated =
      http.Request(drogon::Post, "/api/platforms", generated_body, 201)["data"];
  const auto generated_id = generated["id"].asString();
  Check(generated_id.find("lower_") == 0 && generated_id != "lower",
        "server generates omitted platform ID");
  Json::Value rename;
  rename["name"] = "renamed";
  const auto updated =
      http.Request(drogon::Patch, "/api/platforms/" + generated_id, rename);
  Check(updated["data"]["id"] == generated_id &&
            updated["data"]["name"] == "renamed",
        "editing preserves generated ID");
  rename["id"] = "replacement";
  Check(http.Request(drogon::Patch, "/api/platforms/" + generated_id, rename,
                     400)["code"] == 1001,
        "HTTP cannot change platform identity");
  http.Request(drogon::Delete, "/api/platforms/" + generated_id);
  Check(http.Request(drogon::Get, "/api/platforms/lower")["data"]["id"] ==
            "lower",
        "existing explicit platform ID is preserved");
  if (transport == "udp") {
    CheckWebSocketRejection(http_port, thread.getLoop(), http, true);
    CheckWebSocketRejection(http_port, thread.getLoop(), http, false);
  }

  auto events = std::make_shared<Events>();
  auto socket = drogon::WebSocketClient::newWebSocketClient(
      "127.0.0.1", http_port, false, thread.getLoop());
  socket->setMessageHandler([events](std::string&& message, const auto&,
                                     const auto& type) {
    if (type != drogon::WebSocketMessageType::Text)
      return;
    std::lock_guard<std::mutex> lock(events->mutex);
    if (message == "pong")
      events->pong = true;
    if (message.find("catalog_changed") != std::string::npos)
      events->catalog = true;
    if (message.find("playback_changed") != std::string::npos)
      events->playback = true;
    Json::Value event;
    Json::CharReaderBuilder reader;
    std::string errors;
    auto parser = std::unique_ptr<Json::CharReader>(reader.newCharReader());
    if (parser->parse(message.data(), message.data() + message.size(), &event,
                      &errors)) {
      if (event["type"] == "auth_ok")
        events->authenticated = true;
      if (event["type"] == "catalog_accepted" &&
          event["request_id"] == events->catalog_request_id)
        events->catalog_accepted_id = event["operation"]["id"].asString();
      if (event["type"] == "catalog_error" &&
          event["request_id"] == events->catalog_request_id)
        events->catalog_error = message;
      if (event["type"] == "playback_changed" &&
          event["error"]["code"] == 3002 &&
          event["trace_id"] == "http_integration")
        events->rejected_playback = true;
      const auto& operation = event["operation"];
      if (event["type"] == "operation_changed" &&
          operation["expected"] == 1000 &&
          operation["id"] == event["entity_id"]) {
        const auto received = operation["received"].asUInt();
        if (operation["state"] == "running" && received > 0 && received < 1000)
          events->catalog_progress_id = operation["id"].asString();
        if (operation["state"] == "succeeded" && received == 1000)
          events->catalog_completed_id = operation["id"].asString();
      }
    }
    events->changed.notify_all();
  });
  auto connected = std::make_shared<std::promise<bool>>();
  auto connection = connected->get_future();
  auto handshake = drogon::HttpRequest::newHttpRequest();
  handshake->setPath("/api/events");
  socket->connectToServer(
      handshake,
      [connected](auto result, const auto& response, const auto& client) {
        const bool success = result == drogon::ReqResult::Ok && response &&
                             response->statusCode() == 101;
        if (success)
          client->getConnection()->send(R"({"type":"auth","token":")" + kToken +
                                        R"("})");
        connected->set_value(success);
      });
  Check(
      connection.wait_for(5s) == std::future_status::ready && connection.get(),
      "WebSocket upgrade");
  {
    std::unique_lock<std::mutex> lock(events->mutex);
    Check(events->changed.wait_for(lock, 3s,
                                   [&] { return events->authenticated; }),
          "WebSocket first-message authentication");
  }
  socket->getConnection()->send("ping");
  {
    std::unique_lock<std::mutex> lock(events->mutex);
    Check(events->changed.wait_for(lock, 3s, [&] { return events->pong; }),
          "authenticated WebSocket ping");
  }

  auto sync_catalog = [&](const std::string& request_id) {
    std::unique_lock<std::mutex> lock(events->mutex);
    events->catalog_request_id = request_id;
    events->catalog_accepted_id.clear();
    events->catalog_error.clear();
    socket->getConnection()->send(R"({"type":"catalog_sync","request_id":")" +
                                  request_id + R"(","platform_id":"lower"})");
    const bool replied = events->changed.wait_for(lock, 5s, [&] {
      return !events->catalog_accepted_id.empty() ||
             !events->catalog_error.empty();
    });
    Check(replied && events->catalog_error.empty(),
          "WebSocket catalog command " + request_id + ": " +
              events->catalog_error);
    return events->catalog_accepted_id;
  };

  auto arguments =
      std::vector<std::string>{Environment("GB_SIMULATOR"),
                               "-server",
                               "127.0.0.1:" + std::to_string(sip_port),
                               "-transport",
                               transport,
                               "-media",
                               Environment("GB_SAMPLE")};
  if (!independent)
    arguments.insert(arguments.end(), {"-ffmpeg", Environment("GB_FFMPEG")});
  auto device = std::make_unique<gb28181::test::ChildProcess>(
      arguments, (directory / ("http-peer-" + transport + ".log")).string());
  Until(
      [&] {
        return http.Request(drogon::Get,
                            "/api/platforms/lower")["data"]["state"] ==
               "online";
      },
      "simulator did not register via HTTP configured platform");
  http.Request(drogon::Post, "/api/platforms/lower/catalog-sync", {}, 404);
  const auto operation = sync_catalog("initial-catalog");
  Until(
      [&] {
        return http.Request(drogon::Get,
                            "/api/operations/" + operation)["data"]["state"] ==
               "succeeded";
      },
      "catalog not completed");
  const auto resources = http.Request(drogon::Get, "/api/resources")["data"];
  Check(resources.size() == 2, "HTTP catalog count");
  Check(
      http.Request(drogon::Get, "/api/resources?state=offline")["data"].empty(),
      "HTTP resource state filter");
  const auto resource_id = resources[0]["id"].asString();
  const auto resource_path = "/api/resources/" + resource_id;
  http.Request(drogon::Get, resource_path);
  Check(http.Request(drogon::Get, resource_path + "/info", {}, 404)["code"] ==
            1004,
        "HTTP unqueried cache missing");
  Check(http.Request(drogon::Post,
                     resource_path + "/info-refresh")["data"]["manufacturer"] ==
            (independent ? "gb28181-go" : "LocalSimulator"),
        "HTTP device information");
  Check(http.Request(drogon::Post,
                     resource_path + "/status-refresh")["data"]["state"] ==
            "online",
        "HTTP device status");
  Json::Value start;
  start["platform_id"] = resources[0]["platform_id"];
  start["channel_gb_id"] = resources[0]["gb_id"];
  auto incomplete = start;
  incomplete.removeMember("channel_gb_id");
  http.Request(drogon::Post, "/api/playbacks", incomplete, 400);
  auto mixed = start;
  mixed["resource_id"] = resource_id;
  http.Request(drogon::Post, "/api/playbacks", mixed, 400);
  auto wrong_platform = start;
  wrong_platform["platform_id"] = "other_lower";
  http.Request(drogon::Post, "/api/playbacks", wrong_platform, 404);
  for (const std::string mode : {"udp", "tcp_passive", "tcp_active"}) {
    Json::Value defaults;
    defaults["default_receive_mode"] = mode;
    Check(http.Request(drogon::Patch, "/api/platforms/lower",
                       defaults)["data"]["default_receive_mode"] == mode,
          "platform mode persisted through HTTP");
    const auto first =
        http.Request(drogon::Post, "/api/playbacks", start, 202)["data"];
    defaults["default_receive_mode"] = mode == "udp" ? "tcp_active" : "udp";
    http.Request(drogon::Patch, "/api/platforms/lower", defaults);
    Json::Value legacy_start;
    legacy_start["resource_id"] = resource_id;
    legacy_start["mode"] = mode;
    const auto second =
        http.Request(drogon::Post, "/api/playbacks", legacy_start, 202)["data"];
    Check(first["playback_id"] == second["playback_id"] &&
              first["id"] != second["id"],
          "HTTP shared upstream");
    const auto playback_path =
        "/api/playbacks/" + first["playback_id"].asString();
    Json::Value playing;
    Until(
        [&] {
          playing = http.Request(drogon::Get, playback_path)["data"];
          return playing["state"] == "playing";
        },
        "HTTP media not ready");
    Check(playing["mode"] == mode,
          "omitted mode inherits platform default; edit leaves live stream "
          "intact");
    gb28181::test::ChildProcess decoder(
        {Environment("GB_FFMPEG"), "-nostdin", "-hide_banner", "-loglevel",
         "error", "-rtsp_transport", "tcp", "-i",
         playing["endpoints"][0]["url"].asString(), "-frames:v", "1", "-f",
         "null", "-"});
    Check(decoder.Wait(15s) == 0, "HTTP endpoint frame decode");
    http.Request(drogon::Post,
                 "/api/playback-leases/" + second["id"].asString() + "/renew");
    http.Request(drogon::Delete,
                 "/api/playback-leases/" + first["id"].asString());
    Check(http.Request(drogon::Get, playback_path)["data"]["consumers"] == 1,
          "HTTP retained consumer");
    http.Request(drogon::Delete,
                 "/api/playback-leases/" + second["id"].asString());
    Check(http.Request(drogon::Get, playback_path)["data"]["state"] == "ended",
          "HTTP final release");
    const auto stats = http.Request(drogon::Get, "/api/health")["data"];
    Check(stats["active_receivers"] == 0 && stats["active_playbacks"] == 0 &&
              stats["playback_leases"] == 0,
          "HTTP resources returned to baseline");
    Until(
        [&] {
          const auto sip = http.Request(drogon::Get, "/api/health")["data"];
          return sip["sip_sessions"] == 0 && sip["sip_requests"] == 0;
        },
        "HTTP SIP resources returned to baseline");
    std::cout << "passed HTTP SIP=" << transport << " RTP=" << mode
              << std::endl;
  }
  if (!independent && transport == "udp") {
    for (const std::string protocol : {"rtsp", "http-flv", ""}) {
      const auto lease =
          http.Request(drogon::Post, "/api/playbacks", start, 202)["data"];
      const auto path = "/api/playbacks/" + lease["playback_id"].asString();
      const auto address_path = path + "/address";
      Check(http.Request(drogon::Post,
                         address_path)["data"]["idle_timeout_seconds"] == 60,
            "HTTP playback address defaults to 60 seconds");
      Json::Value timeout;
      timeout["idle_timeout_seconds"] = 0;
      http.Request(drogon::Post, address_path, timeout, 400);
      Json::Value playback;
      Until(
          [&] {
            playback = http.Request(drogon::Get, path)["data"];
            return playback["state"] == "playing";
          },
          "managed HTTP playback becomes ready");
      std::unique_ptr<gb28181::test::ChildProcess> reader;
      if (!protocol.empty()) {
        std::vector<std::string> command{Environment("GB_FFMPEG"), "-nostdin",
                                         "-hide_banner", "-loglevel", "error"};
        if (protocol == "rtsp")
          command.insert(command.end(), {"-rtsp_transport", "tcp"});
        std::string url;
        for (const auto& endpoint : playback["endpoints"])
          if (endpoint["protocol"] == protocol)
            url = endpoint["url"].asString();
        Check(!url.empty(), "managed HTTP playback endpoint");
        command.insert(command.end(), {"-re", "-i", url, "-t", "8", "-progress",
                                       "pipe:1", "-f", "null", "-"});
        const auto progress_path = directory / ("idle-" + protocol + ".log");
        reader = std::make_unique<gb28181::test::ChildProcess>(
            command, progress_path.string());
        Until(
            [&] {
              std::ifstream progress(progress_path);
              for (std::string line; std::getline(progress, line);) {
                if (line.find("progress=continue") == 0)
                  return true;
              }
              return false;
            },
            "managed " + protocol + " reader starts decoding");
      }
      timeout["idle_timeout_seconds"] = 2;
      Check(http.Request(drogon::Post, address_path,
                         timeout)["data"]["idle_timeout_seconds"] == 2,
            "HTTP playback address accepts custom idle timeout");
      http.Request(drogon::Delete,
                   "/api/playback-leases/" + lease["id"].asString());
      if (reader) {
        std::this_thread::sleep_for(3s);
        Check(http.Request(drogon::Get, path)["data"]["state"] == "playing",
              "actual " + protocol + " reader keeps playback without leases");
        Check(reader->Wait(12s) == 0, protocol + " reader decodes");
      }
      Check(http.Request(drogon::Get, path)["data"]["state"] == "playing",
            "managed playback retains the idle grace period");
      Until(
          [&] {
            return http.Request(drogon::Get, path)["data"]["state"] == "ended";
          },
          "managed playback ends after no readers for its idle timeout");
      Check(http.Request(drogon::Get,
                         "/api/health")["data"]["active_receivers"] == 0,
            "idle playback releases the RTP receiver");
    }
  }
  if (!independent) {
    device.reset();
    Until(
        [&] {
          return http.Request(drogon::Get,
                              "/api/platforms/lower")["data"]["state"] ==
                 "offline";
        },
        "HTTP offline detection");
    device =
        std::make_unique<gb28181::test::ChildProcess>(std::vector<std::string>{
            Environment("GB_SIMULATOR"), "-server",
            "127.0.0.1:" + std::to_string(sip_port), "-transport", transport,
            "-invite-mode", "reject"});
    Until(
        [&] {
          return http.Request(drogon::Get,
                              "/api/platforms/lower")["data"]["state"] ==
                 "online";
        },
        "HTTP registration recovery");
    const auto& rejected_start = start;
    const auto rejected = http.Request(drogon::Post, "/api/playbacks",
                                       rejected_start, 202)["data"];
    Json::Value failed;
    Until(
        [&] {
          failed = http.Request(
              drogon::Get,
              "/api/playbacks/" + rejected["playback_id"].asString())["data"];
          return failed["state"] == "failed";
        },
        "HTTP asynchronous rejection");
    Check(failed["error"]["code"] == 3002 &&
              failed["error"]["trace_id"] == "http_integration",
          "accepted HTTP playback retains SDK error and trace");
  }
  {
    std::unique_lock<std::mutex> lock(events->mutex);
    Check(events->changed.wait_for(
              lock, 3s,
              [&] {
                return events->catalog && events->playback &&
                       (independent || events->rejected_playback);
              }),
          "WebSocket catalog, playback and asynchronous failure events");
  }
  if (!independent && transport == "udp") {
    device.reset();
    Until(
        [&] {
          return http.Request(drogon::Get,
                              "/api/platforms/lower")["data"]["state"] ==
                 "offline";
        },
        "large HTTP fixture disconnect");
    auto large_arguments = arguments;
    large_arguments.insert(large_arguments.end(),
                           {"-channels", "1000", "-catalog-batch", "17"});
    device = std::make_unique<gb28181::test::ChildProcess>(large_arguments);
    Until(
        [&] {
          return http.Request(drogon::Get,
                              "/api/platforms/lower")["data"]["state"] ==
                 "online";
        },
        "large HTTP fixture registration");
    const auto began = std::chrono::steady_clock::now();
    const auto operation_id = sync_catalog("large-catalog");
    {
      std::unique_lock<std::mutex> lock(events->mutex);
      const bool completed = events->changed.wait_for(lock, 10s, [&] {
        return events->catalog_progress_id == operation_id &&
               events->catalog_completed_id == operation_id;
      });
      Check(completed,
            "large catalog WebSocket events: accepted=" + operation_id +
                " progress=" + events->catalog_progress_id +
                " completed=" + events->catalog_completed_id);
    }
    const auto synchronized = std::chrono::steady_clock::now();
    const auto large =
        http.Request(drogon::Get, "/api/resources?limit=1000")["data"];
    Check(large.size() == 1000, "HTTP returns all 1000 catalog rows");
    std::cout << "1000-channel WebSocket sync ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     synchronized - began)
                     .count()
              << " HTTP list us="
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::steady_clock::now() - synchronized)
                     .count()
              << std::endl;
  }
  socket->stop();
  socket.reset();
  http.Request(drogon::Delete, "/api/platforms/lower");
}
}  // namespace

int main() {
  try {
    trantor::Logger::setLogLevel(trantor::Logger::kInfo);
    const std::filesystem::path directory = Environment("TEST_TMPDIR");
    Test("udp", directory);
    Test("tcp", directory);
    std::cout << "HTTP and WebSocket end-to-end passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
