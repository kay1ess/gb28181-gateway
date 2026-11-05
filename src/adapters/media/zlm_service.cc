#include "src/adapters/media/zlm_service.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>
#include <utility>

#include <mk_mediakit.h>

namespace gb28181::media {
namespace {
struct SharedListenerHandle {
  explicit SharedListenerHandle(mk_rtp_shared_server value) : value(value) {
  }

  ~SharedListenerHandle() {
    if (value)
      mk_rtp_shared_server_release(value);
  }

  mk_rtp_shared_server value;
};

struct RtpHandle {
  explicit RtpHandle(std::shared_ptr<SharedListenerHandle> listener)
      : listener(std::move(listener)) {
  }

  ~RtpHandle() {
    if (auto handle = value.load())
      mk_rtp_shared_stream_release(handle);
  }

  std::shared_ptr<SharedListenerHandle> listener;
  std::atomic<mk_rtp_shared_stream> value{nullptr};
  Receiver snapshot;
};

// This registry is required because mk_events has no user_data parameter.
// Callbacks are installed once, and only the weak routing target changes.
struct EngineRegistry {
  std::mutex mutex;
  std::weak_ptr<ZlmState> active;
  std::once_flag initialize;
};

EngineRegistry& Registry() {
  static EngineRegistry registry;
  return registry;
}

Error Failure(ErrorCode code) {
  return {code, ErrorMessage(code), {}};
}

bool Identifier(const std::string& text) {
  return !text.empty() && text.size() <= 128 &&
         std::all_of(text.begin(), text.end(), [](unsigned char c) {
           return std::isalnum(c) || c == '_' || c == '-';
         });
}
}  // namespace

struct ZlmState {
  mutable std::mutex mutex;
  bool running = false;
  Config config;
  uint16_t http_port = 0;
  uint16_t rtsp_port = 0;
  uint16_t rtp_port = 0;
  std::shared_ptr<SharedListenerHandle> listener;
  std::function<void(MediaEvent)> handler;
  std::map<ReceiverId, std::shared_ptr<RtpHandle>> receivers;
};

namespace {
void Emit(const std::shared_ptr<ZlmState>& state, MediaEvent event) {
  std::function<void(MediaEvent)> handler;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->running)
      return;
    handler = state->handler;
  }
  if (handler) {
    try {
      handler(std::move(event));
    } catch (...) {
    }
  }
}

std::shared_ptr<ZlmState> ActiveState() {
  auto& registry = Registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  return registry.active.lock();
}

void API_CALL MediaChanged(int registered, const mk_media_source source) {
  try {
    auto state = ActiveState();
    if (!state || std::string(mk_media_source_get_app(source)) != "rtp" ||
        std::string(mk_media_source_get_schema(source)) != "rtsp")
      return;
    const std::string id = mk_media_source_get_stream(source);
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      auto found = state->receivers.find(id);
      if (!state->running || found == state->receivers.end())
        return;
      auto& snapshot = found->second->snapshot;
      if (!registered && !snapshot.ready)
        return;
      snapshot.ready = registered != 0;
      snapshot.codecs.clear();
      if (registered) {
        const int count = mk_media_source_get_track_count(source);
        for (int index = 0; index < count; ++index) {
          auto track = mk_media_source_get_track(source, index);
          if (track) {
            snapshot.codecs.emplace_back(mk_track_codec_name(track));
            mk_track_unref(track);
          }
        }
      }
    }
    Emit(state, {id,
                 registered ? MediaEventType::kReady : MediaEventType::kClosed,
                 {ErrorCode::kOk, {}, {}}});
  } catch (...) {
  }
}

struct CallbackContext {
  std::weak_ptr<ZlmState> state;
  ReceiverId id;
};

void API_CALL FreeContext(void* data) {
  delete static_cast<CallbackContext*>(data);
}

void API_CALL Detached(void* data) {
  try {
    auto& context = *static_cast<CallbackContext*>(data);
    if (auto state = context.state.lock())
      Emit(state, {context.id, MediaEventType::kTimeout,
                   Failure(ErrorCode::kMediaReceiveTimeout)});
  } catch (...) {
  }
}

void API_CALL Connected(void* data, int error, const char*, int) {
  try {
    auto& context = *static_cast<CallbackContext*>(data);
    auto state = context.state.lock();
    if (!state)
      return;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      auto found = state->receivers.find(context.id);
      if (!state->running || found == state->receivers.end())
        return;
      found->second->snapshot.connected = error == 0;
    }
    Emit(state, {context.id,
                 error ? MediaEventType::kConnectionFailed
                       : MediaEventType::kConnected,
                 error ? Failure(ErrorCode::kMediaConnectFailed)
                       : Error{ErrorCode::kOk, {}, {}}});
  } catch (...) {
  }
}
}  // namespace

ZlmMediaService::ZlmMediaService() : state_(std::make_shared<ZlmState>()) {
}

ZlmMediaService::~ZlmMediaService() {
  Stop();
}

Result<void> ZlmMediaService::Start(const Config& config,
                                    std::function<void(MediaEvent)> handler) {
  auto& registry = Registry();
  std::lock_guard<std::mutex> engine_lock(registry.mutex);
  if (!registry.active.expired())
    return Failure(ErrorCode::kInvalidState);
  if (config.rtp_port == 65535 ||
      (config.rtp_port && (config.rtp_port == config.sip_port ||
                           config.rtp_port + 1 == config.sip_port ||
                           config.rtp_port == config.media_http_port ||
                           config.rtp_port == config.media_rtsp_port)))
    return Failure(ErrorCode::kInvalidArgument);
  try {
    std::call_once(registry.initialize, [] {
      mk_config options{};
      options.thread_num = 2;
      options.log_level = 3;
      options.log_mask = LOG_CONSOLE;
      mk_env_init(&options);
      // DLL static destructors run under the Windows loader lock. Join ZLM's
      // process-global workers while normal execution is still permitted.
#ifdef _WIN32
      std::atexit([] { mk_env_shutdown(); });
#endif
      mk_events events{};
      events.on_mk_media_changed = MediaChanged;
      mk_events_listen(&events);
    });
    mk_set_option("general.listen_ip", config.bind_address.c_str());
    mk_set_option("protocol.enable_rtsp", "1");
    // HTTP-FLV uses ZLM's RTMP media source without an RTMP listening server.
    mk_set_option("protocol.enable_rtmp", "1");
    // The bundled smsplayer FLV demuxer accepts legacy HEVC codec ID 12,
    // whereas ZLM defaults to Enhanced RTMP/FLV's hvc1 FourCC header.
    mk_set_option("rtmp.enhanced", "0");
    mk_set_option("protocol.enable_hls", "0");
    mk_set_option("protocol.enable_mp4", "0");
    mk_set_option("protocol.enable_audio", "1");
    mk_set_option(
        "rtp_proxy.timeoutSec",
        std::to_string(std::max(1u, config.media_timeout_ms / 1000)).c_str());
    const auto http = mk_http_server_start(config.media_http_port, 0);
    const auto rtsp = mk_rtsp_server_start(config.media_rtsp_port, 0);
    if (!http || !rtsp) {
      mk_stop_all_server();
      return Failure(ErrorCode::kMediaAllocationFailed);
    }
    auto listener =
        std::make_shared<SharedListenerHandle>(mk_rtp_shared_server_create(
            config.rtp_port, config.bind_address.c_str()));
    if (!listener->value) {
      mk_stop_all_server();
      return Failure(ErrorCode::kMediaAllocationFailed);
    }
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->config = config;
      state_->http_port = http;
      state_->rtsp_port = rtsp;
      state_->rtp_port = mk_rtp_shared_server_port(listener->value);
      state_->listener = std::move(listener);
      state_->handler = std::move(handler);
      state_->running = true;
    }
    registry.active = state_;
    return {};
  } catch (...) {
    mk_stop_all_server();
    return Failure(ErrorCode::kMediaAllocationFailed);
  }
}

void ZlmMediaService::Stop() {
  std::map<ReceiverId, std::shared_ptr<RtpHandle>> retired;
  std::shared_ptr<SharedListenerHandle> listener;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->running)
      return;
    state_->running = false;
    state_->handler = {};
    retired.swap(state_->receivers);
    listener = std::move(state_->listener);
  }
  // Releasing handles can emit synchronous callbacks: never hold state mutex.
  retired.clear();
  listener.reset();
  mk_stop_all_server();
  auto& registry = Registry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  if (registry.active.lock() == state_)
    registry.active.reset();
}

Result<Receiver> ZlmMediaService::CreateReceiver(
    const ReceiverOptions& options) {
  if (!Identifier(options.id) || !options.ssrc ||
      (options.mode != RtpReceiveMode::kUdp &&
       options.mode != RtpReceiveMode::kTcpPassive &&
       options.mode != RtpReceiveMode::kTcpActive))
    return Failure(ErrorCode::kInvalidArgument);
  std::shared_ptr<RtpHandle> receiver;
  try {
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (!state_->running)
        return Failure(ErrorCode::kInvalidState);
      if (state_->receivers.count(options.id))
        return Failure(ErrorCode::kAlreadyExists);
      if (options.port && options.port != state_->rtp_port)
        return Failure(ErrorCode::kInvalidArgument);
      for (const auto& entry : state_->receivers)
        if (entry.second->snapshot.options.ssrc == options.ssrc)
          return Failure(ErrorCode::kAlreadyExists);
      if (state_->receivers.size() >= state_->config.max_playbacks)
        return Failure(ErrorCode::kResourceExhausted);
      receiver = std::make_shared<RtpHandle>(state_->listener);
      receiver->snapshot.options = options;
      receiver->snapshot.port = state_->rtp_port;
      receiver->snapshot.endpoints.push_back(
          {"rtsp", "rtsp://" + state_->config.advertised_address + ":" +
                       std::to_string(state_->rtsp_port) + "/rtp/" +
                       options.id});
      receiver->snapshot.endpoints.push_back(
          {"http-flv", "http://" + state_->config.advertised_address + ":" +
                           std::to_string(state_->http_port) + "/rtp/" +
                           options.id + ".live.flv"});
      state_->receivers.emplace(options.id, receiver);
    }
    // Native registration runs on the shared media poller. Never hold the
    // state mutex while waiting: that poller can emit MediaChanged callbacks.
    const int mode = options.mode == RtpReceiveMode::kUdp          ? 0
                     : options.mode == RtpReceiveMode::kTcpPassive ? 1
                                                                   : 2;
    receiver->value = mk_rtp_shared_stream_create(
        receiver->listener->value, options.id.c_str(), options.ssrc, mode);
    if (receiver->value)
      mk_rtp_shared_stream_set_on_detach2(
          receiver->value, Detached, new CallbackContext{state_, options.id},
          FreeContext);
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto found = state_->receivers.find(options.id);
    if (!state_->running || found == state_->receivers.end() ||
        found->second != receiver)
      return Failure(ErrorCode::kInvalidState);
    if (!receiver->value) {
      state_->receivers.erase(found);
      return Failure(ErrorCode::kMediaAllocationFailed);
    }
    return receiver->snapshot;
  } catch (...) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto found = state_->receivers.find(options.id);
    if (found != state_->receivers.end() && found->second == receiver)
      state_->receivers.erase(found);
    return Failure(ErrorCode::kMediaAllocationFailed);
  }
}

Result<void> ZlmMediaService::ConnectReceiver(const ReceiverId& id,
                                              const std::string& address,
                                              uint16_t port) {
  std::shared_ptr<RtpHandle> receiver;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto found = state_->receivers.find(id);
    if (!state_->running || found == state_->receivers.end())
      return Failure(ErrorCode::kNotFound);
    receiver = found->second;
  }
  if (receiver->snapshot.options.mode != RtpReceiveMode::kTcpActive ||
      address.empty() || !port)
    return Failure(ErrorCode::kInvalidArgument);
  if (!receiver->value.load())
    return Failure(ErrorCode::kInvalidState);
  try {
    mk_rtp_shared_stream_connect2(receiver->value, address.c_str(), port,
                                  Connected, new CallbackContext{state_, id},
                                  FreeContext);
    return {};
  } catch (...) {
    return Failure(ErrorCode::kMediaConnectFailed);
  }
}

Result<void> ZlmMediaService::CloseReceiver(const ReceiverId& id) {
  std::shared_ptr<RtpHandle> retired;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto found = state_->receivers.find(id);
    if (found == state_->receivers.end())
      return {};
    retired = std::move(found->second);
    state_->receivers.erase(found);
  }
  return {};
}

Result<Receiver> ZlmMediaService::GetReceiver(const ReceiverId& id) {
  std::lock_guard<std::mutex> lock(state_->mutex);
  auto found = state_->receivers.find(id);
  if (found == state_->receivers.end())
    return Failure(ErrorCode::kNotFound);
  return found->second->snapshot;
}

uint32_t ZlmMediaService::ReceiverCount() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return static_cast<uint32_t>(state_->receivers.size());
}
}  // namespace gb28181::media
