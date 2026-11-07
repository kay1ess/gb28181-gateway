#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <drogon/drogon.h>

#include "apps/server/http/server.h"
#include "gb28181/client.h"

namespace {
// Ctrl+C runs on a Windows console thread. Quit the HTTP loop there, and force
// the process out if joining media or SIP threads does not return.
void RequestShutdown() {
  static std::atomic<bool> stopping{false};
  if (stopping.exchange(true))
    return;
  std::thread([] {
    drogon::app().quit();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::_Exit(0);
  }).detach();
}

#ifdef _WIN32
BOOL WINAPI OnConsoleCtrl(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
      type == CTRL_CLOSE_EVENT || type == CTRL_SHUTDOWN_EVENT) {
    RequestShutdown();
    return TRUE;
  }
  return FALSE;
}
#endif
}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: gateway <configuration.json>\n";
    return 2;
  }
  std::shared_ptr<gb28181::Client> client = gb28181::CreateClient();
  try {
    std::ifstream file(argv[1]);
    Json::Value input;
    Json::CharReaderBuilder reader;
    std::string errors;
    if (!file || !Json::parseFromStream(reader, file, &input, &errors) ||
        !input.isObject())
      throw std::runtime_error("Invalid configuration file");
    gb28181::Config config;
    config.database_path =
        input.get("database_path", config.database_path).asString();
    config.server_gb_id =
        input.get("server_gb_id", config.server_gb_id).asString();
    config.realm = input.get("realm", config.realm).asString();
    config.bind_address =
        input.get("bind_address", config.bind_address).asString();
    config.advertised_address =
        input.get("advertised_address", config.advertised_address).asString();
    auto port = [&](const char* name, uint16_t fallback) {
      const auto value = input.get(name, fallback);
      if (!value.isUInt() || value.asUInt() > 65535)
        throw std::runtime_error("Invalid port");
      return static_cast<uint16_t>(value.asUInt());
    };
    config.sip_port = port("sip_port", config.sip_port);
    config.media_http_port = port("media_http_port", config.media_http_port);
    config.media_rtsp_port = port("media_rtsp_port", config.media_rtsp_port);
    config.rtp_port = port("rtp_port", config.rtp_port);
    config.query_timeout_ms =
        input.get("query_timeout_ms", config.query_timeout_ms).asUInt();
    config.media_timeout_ms =
        input.get("media_timeout_ms", config.media_timeout_ms).asUInt();
    config.lease_seconds =
        input.get("lease_seconds", config.lease_seconds).asUInt();
    config.max_playbacks =
        input.get("max_playbacks", config.max_playbacks).asUInt();
    const auto http_port = port("http_port", 8080);
    if (config.rtp_port && http_port == config.rtp_port)
      throw std::runtime_error("HTTP port conflicts with RTP TCP listener");
    const auto token = input.get("api_token", "").asString();
    auto started = client->Start(config);
    if (!started)
      throw std::runtime_error(started.error().message);
    gb28181::http::RegisterRoutes(client, token);
    auto& app = drogon::app()
                    .setLogLevel(trantor::Logger::kInfo)
                    .setThreadNum(2)
                    .setClientMaxBodySize(1024 * 1024)
                    .addListener(config.bind_address, http_port);
#ifdef _WIN32
    // Drogon's SIGINT handler logs and then shuts down on the console thread.
    // That write can stall a terminal that has stopped reading after Ctrl+C.
    app.disableSigtermHandling();
    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);
#endif
    std::cout << "Listening on http://" << config.bind_address << ':'
              << http_port << " (Ctrl+C to stop)" << std::endl;
    app.run();
    std::cout << "Stopping" << std::endl;
    return client->Stop().ok() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    client->Stop();
    return 1;
  }
}
