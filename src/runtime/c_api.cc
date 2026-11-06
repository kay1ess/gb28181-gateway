#include "gb28181/c_api.h"

#include <chrono>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "gb28181/client.h"
#include "src/adapters/media/zlm_service.h"
#include "src/core/sdk_json.h"

struct gb28181_client {
  std::unique_ptr<gb28181::Client> client;
};

struct gb28181_cancel {
  gb28181::Cancellation state;
};

namespace {
using gb28181::Client;
using gb28181::Config;
using gb28181::ErrorCode;
using gb28181::OnlineState;
using gb28181::PlatformConfig;
using gb28181::RequestOptions;
using gb28181::ResourceFilter;
using gb28181::Result;
using gb28181::RtpReceiveMode;
using gb28181::StartLiveRequest;

static_assert(GB28181_OK == static_cast<gb28181_status>(ErrorCode::kOk));
static_assert(GB28181_INVALID_ARGUMENT ==
              static_cast<gb28181_status>(ErrorCode::kInvalidArgument));
static_assert(GB28181_UNAUTHENTICATED ==
              static_cast<gb28181_status>(ErrorCode::kUnauthenticated));
static_assert(GB28181_PERMISSION_DENIED ==
              static_cast<gb28181_status>(ErrorCode::kPermissionDenied));
static_assert(GB28181_NOT_FOUND ==
              static_cast<gb28181_status>(ErrorCode::kNotFound));
static_assert(GB28181_ALREADY_EXISTS ==
              static_cast<gb28181_status>(ErrorCode::kAlreadyExists));
static_assert(GB28181_INVALID_STATE ==
              static_cast<gb28181_status>(ErrorCode::kInvalidState));
static_assert(GB28181_CANCELLED ==
              static_cast<gb28181_status>(ErrorCode::kCancelled));
static_assert(GB28181_TIMEOUT ==
              static_cast<gb28181_status>(ErrorCode::kTimeout));
static_assert(GB28181_RESOURCE_EXHAUSTED ==
              static_cast<gb28181_status>(ErrorCode::kResourceExhausted));
static_assert(GB28181_INTERNAL_ERROR ==
              static_cast<gb28181_status>(ErrorCode::kInternalError));
static_assert(GB28181_PLATFORM_OFFLINE ==
              static_cast<gb28181_status>(ErrorCode::kPlatformOffline));
static_assert(GB28181_RESOURCE_OFFLINE ==
              static_cast<gb28181_status>(ErrorCode::kResourceOffline));
static_assert(GB28181_CATALOG_SYNC_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kCatalogSyncFailed));
static_assert(GB28181_SIP_TRANSPORT_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kSipTransportFailed));
static_assert(GB28181_SIP_REQUEST_REJECTED ==
              static_cast<gb28181_status>(ErrorCode::kSipRequestRejected));
static_assert(GB28181_SDP_NEGOTIATION_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kSdpNegotiationFailed));
static_assert(GB28181_MEDIA_ALLOCATION_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kMediaAllocationFailed));
static_assert(GB28181_MEDIA_CONNECT_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kMediaConnectFailed));
static_assert(GB28181_MEDIA_RECEIVE_TIMEOUT ==
              static_cast<gb28181_status>(ErrorCode::kMediaReceiveTimeout));
static_assert(GB28181_UNSUPPORTED_MEDIA ==
              static_cast<gb28181_status>(ErrorCode::kUnsupportedMedia));
static_assert(GB28181_STORAGE_BUSY ==
              static_cast<gb28181_status>(ErrorCode::kStorageBusy));
static_assert(GB28181_STORAGE_READ_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kStorageReadFailed));
static_assert(GB28181_STORAGE_WRITE_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kStorageWriteFailed));
static_assert(GB28181_STORAGE_MIGRATION_FAILED ==
              static_cast<gb28181_status>(ErrorCode::kStorageMigrationFailed));

gb28181_status Status(const Result<void>& result) {
  return result ? static_cast<gb28181_status>(GB28181_OK)
                : static_cast<gb28181_status>(result.error().code);
}

template <typename Work>
gb28181_status Protect(Work work) noexcept {
  try {
    return work();
  } catch (const std::bad_alloc&) {
    return GB28181_RESOURCE_EXHAUSTED;
  } catch (...) {
    return GB28181_INTERNAL_ERROR;
  }
}

const char* OrEmpty(const char* value) {
  return value ? value : "";
}

Config Convert(const gb28181_config* value) {
  Config config;
  if (!value)
    return config;
  if (value->database_path)
    config.database_path = value->database_path;
  if (value->server_gb_id)
    config.server_gb_id = value->server_gb_id;
  if (value->realm)
    config.realm = value->realm;
  if (value->bind_address)
    config.bind_address = value->bind_address;
  if (value->advertised_address)
    config.advertised_address = value->advertised_address;
  config.sip_port = value->sip_port;
  config.media_http_port = value->media_http_port;
  config.media_rtsp_port = value->media_rtsp_port;
  config.rtp_port = value->rtp_port;
  config.query_timeout_ms = value->query_timeout_ms;
  config.media_timeout_ms = value->media_timeout_ms;
  config.lease_seconds = value->lease_seconds;
  config.max_playbacks = value->max_playbacks;
  return config;
}

bool ValidMode(int32_t mode) {
  return mode == GB28181_MODE_UDP || mode == GB28181_MODE_TCP_PASSIVE ||
         mode == GB28181_MODE_TCP_ACTIVE;
}

RtpReceiveMode ConvertMode(int32_t mode) {
  if (mode == GB28181_MODE_TCP_PASSIVE)
    return RtpReceiveMode::kTcpPassive;
  if (mode == GB28181_MODE_TCP_ACTIVE)
    return RtpReceiveMode::kTcpActive;
  return RtpReceiveMode::kUdp;
}

bool Valid(const char* id) {
  return id != nullptr;
}

bool Valid(uint32_t timeout) {
  return timeout <= 86400;
}

std::optional<uint32_t> Convert(uint32_t timeout) {
  if (!timeout)
    return std::nullopt;
  return timeout;
}

std::string Convert(const char* id) {
  return id;
}

bool Valid(const gb28181_platform_config* value) {
  return value && value->enabled <= 1 && ValidMode(value->default_receive_mode);
}

PlatformConfig Convert(const gb28181_platform_config* value) {
  return {OrEmpty(value->id),
          OrEmpty(value->gb_id),
          OrEmpty(value->name),
          OrEmpty(value->realm),
          OrEmpty(value->username),
          OrEmpty(value->password),
          value->enabled != 0,
          value->heartbeat_timeout_seconds,
          ConvertMode(value->default_receive_mode)};
}

bool Valid(const gb28181_resource_filter* value) {
  return !value || (value->state >= GB28181_STATE_ANY &&
                    value->state <= GB28181_STATE_OFFLINE);
}

ResourceFilter Convert(const gb28181_resource_filter* value) {
  ResourceFilter filter;
  if (!value)
    return filter;
  filter.platform_id = OrEmpty(value->platform_id);
  filter.keyword = OrEmpty(value->keyword);
  filter.offset = value->offset;
  filter.limit = value->limit;
  switch (value->state) {
    case GB28181_STATE_UNKNOWN:
      filter.state = OnlineState::kUnknown;
      break;
    case GB28181_STATE_ONLINE:
      filter.state = OnlineState::kOnline;
      break;
    case GB28181_STATE_OFFLINE:
      filter.state = OnlineState::kOffline;
      break;
  }
  return filter;
}

bool Valid(const gb28181_live_request* value) {
  return value && value->resource_id &&
         (value->mode == GB28181_MODE_DEFAULT || ValidMode(value->mode));
}

StartLiveRequest Convert(const gb28181_live_request* value) {
  StartLiveRequest request;
  request.resource_id = value->resource_id;
  if (value->mode != GB28181_MODE_DEFAULT)
    request.mode = ConvertMode(value->mode);
  return request;
}

bool Valid(const gb28181_channel_live_request* value) {
  return value && value->platform_id && *value->platform_id &&
         value->channel_gb_id && *value->channel_gb_id &&
         (value->mode == GB28181_MODE_DEFAULT || ValidMode(value->mode));
}

StartLiveRequest Convert(const gb28181_channel_live_request* value) {
  StartLiveRequest request;
  request.platform_id = value->platform_id;
  request.channel_gb_id = value->channel_gb_id;
  if (value->mode != GB28181_MODE_DEFAULT)
    request.mode = ConvertMode(value->mode);
  return request;
}

RequestOptions Convert(const gb28181_request_options* value) {
  RequestOptions options;
  if (value) {
    if (value->timeout_ms)
      options.timeout = std::chrono::milliseconds(value->timeout_ms);
    options.trace_id = OrEmpty(value->trace_id);
    if (value->cancellation)
      options.cancellation = value->cancellation->state;
  }
  return options;
}

std::string WriteJson(const Json::Value& value) {
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  writer["emitUTF8"] = true;
  return Json::writeString(writer, value);
}

template <typename T>
void Complete(gb28181_completion_fn completion,
              void* user_data,
              const std::string& trace_id,
              const Result<T>& result) noexcept {
  gb28181_status status = GB28181_INTERNAL_ERROR;
  std::string json;
  try {
    Json::Value body;
    if (result) {
      body["code"] = 0;
      body["message"] = "Success";
      body["trace_id"] = trace_id;
      if constexpr (!std::is_void_v<T>)
        body["data"] = gb28181::Serialize(result.value());
      else
        body["data"] = Json::Value();
    } else {
      body = gb28181::Serialize(result.error());
      body["data"] = Json::Value();
    }
    json = WriteJson(body);
    status = result ? static_cast<gb28181_status>(GB28181_OK)
                    : static_cast<gb28181_status>(result.error().code);
  } catch (...) {
  }
  try {
    completion(user_data, status, json.empty() ? nullptr : json.c_str());
  } catch (...) {
  }
}

template <typename Method, typename... Args>
gb28181_status Submit(gb28181_client* client,
                      const gb28181_request_options* options,
                      gb28181_completion_fn completion,
                      void* user_data,
                      Method method,
                      Args... args) {
  if (!client || !completion || !(Valid(args) && ...))
    return GB28181_INVALID_ARGUMENT;
  return Protect([&] {
    RequestOptions request_options = Convert(options);
    auto done = [completion, user_data,
                 trace_id = request_options.trace_id](auto result) {
      Complete(completion, user_data, trace_id, result);
    };
    return Status((client->client.get()->*method)(
        Convert(args)..., std::move(done), std::move(request_options)));
  });
}
}  // namespace

extern "C" {

uint32_t gb28181_abi_version(void) {
  return 1;
}

const char* gb28181_error_message(gb28181_status status) {
  return gb28181::ErrorMessage(static_cast<ErrorCode>(status));
}

gb28181_status gb28181_config_init(gb28181_config* config) {
  if (!config)
    return GB28181_INVALID_ARGUMENT;
  return Protect([&] {
    const Config defaults;
    *config = {};
    config->sip_port = defaults.sip_port;
    config->media_http_port = defaults.media_http_port;
    config->media_rtsp_port = defaults.media_rtsp_port;
    config->rtp_port = defaults.rtp_port;
    config->query_timeout_ms = defaults.query_timeout_ms;
    config->media_timeout_ms = defaults.media_timeout_ms;
    config->lease_seconds = defaults.lease_seconds;
    config->max_playbacks = defaults.max_playbacks;
    return GB28181_OK;
  });
}

gb28181_status gb28181_platform_config_init(gb28181_platform_config* config) {
  if (!config)
    return GB28181_INVALID_ARGUMENT;
  return Protect([&] {
    const PlatformConfig defaults;
    *config = {};
    config->enabled = defaults.enabled ? 1 : 0;
    config->heartbeat_timeout_seconds = defaults.heartbeat_timeout_seconds;
    config->default_receive_mode = GB28181_MODE_UDP;
    return GB28181_OK;
  });
}

gb28181_status gb28181_resource_filter_init(gb28181_resource_filter* filter) {
  if (!filter)
    return GB28181_INVALID_ARGUMENT;
  return Protect([&] {
    const ResourceFilter defaults;
    *filter = {};
    filter->limit = defaults.limit;
    filter->state = GB28181_STATE_ANY;
    return GB28181_OK;
  });
}

gb28181_status gb28181_client_create(gb28181_client** out_client) {
  if (!out_client)
    return GB28181_INVALID_ARGUMENT;
  *out_client = nullptr;
  return Protect([&] {
    auto handle = std::make_unique<gb28181_client>();
    handle->client = gb28181::CreateClient();
    *out_client = handle.release();
    return GB28181_OK;
  });
}

gb28181_status gb28181_client_start(gb28181_client* client,
                                    const gb28181_config* config) {
  if (!client)
    return GB28181_INVALID_ARGUMENT;
  return Protect(
      [&] { return Status(client->client->Start(Convert(config))); });
}

gb28181_status gb28181_client_stop(gb28181_client* client) {
  if (!client)
    return GB28181_INVALID_ARGUMENT;
  return Protect([&] { return Status(client->client->Stop()); });
}

gb28181_status gb28181_client_destroy(gb28181_client* client) {
  const gb28181_status status = gb28181_client_stop(client);
  if (status == GB28181_OK)
    delete client;
  return status;
}

gb28181_status gb28181_library_shutdown(void) {
  return Protect(
      [] { return Status(gb28181::media::ShutdownZlmEnvironment()); });
}

gb28181_status gb28181_cancel_create(gb28181_cancel** out_cancel) {
  if (!out_cancel)
    return GB28181_INVALID_ARGUMENT;
  *out_cancel = nullptr;
  return Protect([&] {
    *out_cancel = new gb28181_cancel;
    return GB28181_OK;
  });
}

void gb28181_cancel_request(gb28181_cancel* cancellation) {
  if (cancellation)
    cancellation->state.Cancel();
}

void gb28181_cancel_destroy(gb28181_cancel* cancellation) {
  delete cancellation;
}

gb28181_status gb28181_add_platform(gb28181_client* client,
                                    const gb28181_platform_config* platform,
                                    const gb28181_request_options* options,
                                    gb28181_completion_fn completion,
                                    void* user_data) {
  return Submit(client, options, completion, user_data, &Client::AddPlatform,
                platform);
}

gb28181_status gb28181_update_platform(gb28181_client* client,
                                       const gb28181_platform_config* platform,
                                       const gb28181_request_options* options,
                                       gb28181_completion_fn completion,
                                       void* user_data) {
  return Submit(client, options, completion, user_data, &Client::UpdatePlatform,
                platform);
}

gb28181_status gb28181_remove_platform(gb28181_client* client,
                                       const char* id,
                                       const gb28181_request_options* options,
                                       gb28181_completion_fn completion,
                                       void* user_data) {
  return Submit(client, options, completion, user_data, &Client::RemovePlatform,
                id);
}

gb28181_status gb28181_get_platform(gb28181_client* client,
                                    const char* id,
                                    const gb28181_request_options* options,
                                    gb28181_completion_fn completion,
                                    void* user_data) {
  return Submit(client, options, completion, user_data, &Client::GetPlatform,
                id);
}

gb28181_status gb28181_get_resource(gb28181_client* client,
                                    const char* id,
                                    const gb28181_request_options* options,
                                    gb28181_completion_fn completion,
                                    void* user_data) {
  return Submit(client, options, completion, user_data, &Client::GetResource,
                id);
}

gb28181_status gb28181_sync_catalog(gb28181_client* client,
                                    const char* id,
                                    const gb28181_request_options* options,
                                    gb28181_completion_fn completion,
                                    void* user_data) {
  return Submit(client, options, completion, user_data, &Client::SyncCatalog,
                id);
}

gb28181_status gb28181_get_operation(gb28181_client* client,
                                     const char* id,
                                     const gb28181_request_options* options,
                                     gb28181_completion_fn completion,
                                     void* user_data) {
  return Submit(client, options, completion, user_data, &Client::GetOperation,
                id);
}

gb28181_status gb28181_get_device_info(gb28181_client* client,
                                       const char* id,
                                       const gb28181_request_options* options,
                                       gb28181_completion_fn completion,
                                       void* user_data) {
  return Submit(client, options, completion, user_data, &Client::GetDeviceInfo,
                id);
}

gb28181_status gb28181_get_device_status(gb28181_client* client,
                                         const char* id,
                                         const gb28181_request_options* options,
                                         gb28181_completion_fn completion,
                                         void* user_data) {
  return Submit(client, options, completion, user_data,
                &Client::GetDeviceStatus, id);
}

gb28181_status gb28181_refresh_device_info(
    gb28181_client* client,
    const char* id,
    const gb28181_request_options* options,
    gb28181_completion_fn completion,
    void* user_data) {
  return Submit(client, options, completion, user_data,
                &Client::RefreshDeviceInfo, id);
}

gb28181_status gb28181_refresh_device_status(
    gb28181_client* client,
    const char* id,
    const gb28181_request_options* options,
    gb28181_completion_fn completion,
    void* user_data) {
  return Submit(client, options, completion, user_data,
                &Client::RefreshDeviceStatus, id);
}

gb28181_status gb28181_get_playback(gb28181_client* client,
                                    const char* id,
                                    const gb28181_request_options* options,
                                    gb28181_completion_fn completion,
                                    void* user_data) {
  return Submit(client, options, completion, user_data, &Client::GetPlayback,
                id);
}

gb28181_status gb28181_wait_playback_ready(
    gb28181_client* client,
    const char* id,
    const gb28181_request_options* options,
    gb28181_completion_fn completion,
    void* user_data) {
  return Submit(client, options, completion, user_data,
                &Client::WaitPlaybackReady, id);
}

gb28181_status gb28181_renew_playback(gb28181_client* client,
                                      const char* id,
                                      const gb28181_request_options* options,
                                      gb28181_completion_fn completion,
                                      void* user_data) {
  return Submit(client, options, completion, user_data, &Client::RenewPlayback,
                id);
}

gb28181_status gb28181_release_playback(gb28181_client* client,
                                        const char* id,
                                        const gb28181_request_options* options,
                                        gb28181_completion_fn completion,
                                        void* user_data) {
  return Submit(client, options, completion, user_data,
                &Client::ReleasePlayback, id);
}

gb28181_status gb28181_force_stop_playback(
    gb28181_client* client,
    const char* id,
    const gb28181_request_options* options,
    gb28181_completion_fn completion,
    void* user_data) {
  return Submit(client, options, completion, user_data,
                &Client::ForceStopPlayback, id);
}

gb28181_status gb28181_list_platforms(gb28181_client* client,
                                      const gb28181_request_options* options,
                                      gb28181_completion_fn completion,
                                      void* user_data) {
  return Submit(client, options, completion, user_data, &Client::ListPlatforms);
}

gb28181_status gb28181_get_statistics(gb28181_client* client,
                                      const gb28181_request_options* options,
                                      gb28181_completion_fn completion,
                                      void* user_data) {
  return Submit(client, options, completion, user_data, &Client::GetStatistics);
}

gb28181_status gb28181_list_resources(gb28181_client* client,
                                      const gb28181_resource_filter* filter,
                                      const gb28181_request_options* options,
                                      gb28181_completion_fn completion,
                                      void* user_data) {
  return Submit(client, options, completion, user_data, &Client::ListResources,
                filter);
}

gb28181_status gb28181_start_live(gb28181_client* client,
                                  const gb28181_live_request* request,
                                  const gb28181_request_options* options,
                                  gb28181_completion_fn completion,
                                  void* user_data) {
  return Submit(client, options, completion, user_data, &Client::StartLive,
                request);
}

gb28181_status gb28181_start_live_by_channel(
    gb28181_client* client,
    const gb28181_channel_live_request* request,
    const gb28181_request_options* options,
    gb28181_completion_fn completion,
    void* user_data) {
  return Submit(client, options, completion, user_data, &Client::StartLive,
                request);
}

gb28181_status gb28181_get_playback_address(
    gb28181_client* client,
    const char* playback_id,
    uint32_t idle_timeout_seconds,
    const gb28181_request_options* options,
    gb28181_completion_fn completion,
    void* user_data) {
  return Submit(client, options, completion, user_data,
                &Client::GetPlaybackAddress, playback_id, idle_timeout_seconds);
}

gb28181_status gb28181_subscribe_events(gb28181_client* client,
                                        gb28181_event_fn callback,
                                        void* user_data,
                                        uint64_t* out_subscription) {
  if (!out_subscription)
    return GB28181_INVALID_ARGUMENT;
  *out_subscription = 0;
  if (!client || !callback)
    return GB28181_INVALID_ARGUMENT;
  return Protect([&]() -> gb28181_status {
    auto result = client->client->SubscribeEvents(
        [callback, user_data](gb28181::Event event) {
          try {
            Json::Value body = gb28181::Serialize(event);
            if (event.catalog_batch)
              body["catalog_batch"] = gb28181::Serialize(*event.catalog_batch);
            const std::string json = WriteJson(body);
            callback(user_data, json.c_str());
          } catch (...) {
          }
        });
    if (!result)
      return static_cast<gb28181_status>(result.error().code);
    *out_subscription = result.value();
    return GB28181_OK;
  });
}

gb28181_status gb28181_unsubscribe_events(gb28181_client* client,
                                          uint64_t subscription) {
  if (!client || !subscription)
    return GB28181_INVALID_ARGUMENT;
  return Protect([&] {
    client->client->UnsubscribeEvents(subscription);
    return GB28181_OK;
  });
}

}  // extern "C"
