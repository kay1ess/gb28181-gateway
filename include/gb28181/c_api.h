#ifndef GB28181_INCLUDE_GB28181_C_API_H_
#define GB28181_INCLUDE_GB28181_C_API_H_

#include <stdint.h>

#if defined(_WIN32) && defined(GB28181_C_EXPORTS)
#define GB28181_C_API __declspec(dllexport)
#elif defined(__GNUC__)
#define GB28181_C_API __attribute__((visibility("default")))
#else
#define GB28181_C_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gb28181_client gb28181_client;
typedef struct gb28181_cancel gb28181_cancel;
typedef uint32_t gb28181_status;

enum {
  GB28181_OK = 0,
  GB28181_INVALID_ARGUMENT = 1001,
  GB28181_UNAUTHENTICATED = 1002,
  GB28181_PERMISSION_DENIED = 1003,
  GB28181_NOT_FOUND = 1004,
  GB28181_ALREADY_EXISTS = 1005,
  GB28181_INVALID_STATE = 1006,
  GB28181_CANCELLED = 1007,
  GB28181_TIMEOUT = 1008,
  GB28181_RESOURCE_EXHAUSTED = 1009,
  GB28181_INTERNAL_ERROR = 1099,
  GB28181_PLATFORM_OFFLINE = 2001,
  GB28181_RESOURCE_OFFLINE = 2002,
  GB28181_CATALOG_SYNC_FAILED = 2003,
  GB28181_SIP_TRANSPORT_FAILED = 3001,
  GB28181_SIP_REQUEST_REJECTED = 3002,
  GB28181_SDP_NEGOTIATION_FAILED = 3003,
  GB28181_MEDIA_ALLOCATION_FAILED = 4001,
  GB28181_MEDIA_CONNECT_FAILED = 4002,
  GB28181_MEDIA_RECEIVE_TIMEOUT = 4003,
  GB28181_UNSUPPORTED_MEDIA = 4004,
  GB28181_STORAGE_BUSY = 5001,
  GB28181_STORAGE_READ_FAILED = 5002,
  GB28181_STORAGE_WRITE_FAILED = 5003,
  GB28181_STORAGE_MIGRATION_FAILED = 5004,
};

enum {
  GB28181_MODE_DEFAULT = -1,
  GB28181_MODE_UDP = 0,
  GB28181_MODE_TCP_PASSIVE = 1,
  GB28181_MODE_TCP_ACTIVE = 2,
};

enum {
  GB28181_STATE_ANY = -1,
  GB28181_STATE_UNKNOWN = 0,
  GB28181_STATE_ONLINE = 1,
  GB28181_STATE_OFFLINE = 2,
};

/* These layouts are fixed for ABI version 1. Use the init functions before
 * overriding fields. Input strings are NUL-terminated UTF-8 and are copied
 * during the call. NULL config strings use the C++ SDK defaults. */
typedef struct gb28181_config {
  const char* database_path;
  const char* server_gb_id;
  const char* realm;
  const char* bind_address;
  const char* advertised_address;
  uint16_t sip_port;
  uint16_t media_http_port;
  uint16_t media_rtsp_port;
  uint16_t rtp_port;
  uint32_t query_timeout_ms;
  uint32_t media_timeout_ms;
  uint32_t lease_seconds;
  uint32_t max_playbacks;
} gb28181_config;

typedef struct gb28181_platform_config {
  const char* id;
  const char* gb_id;
  const char* name;
  const char* realm;
  const char* username;
  const char* password;
  uint32_t enabled;
  uint32_t heartbeat_timeout_seconds;
  int32_t default_receive_mode;
} gb28181_platform_config;

typedef struct gb28181_resource_filter {
  const char* platform_id;
  const char* keyword;
  uint32_t offset;
  uint32_t limit;
  int32_t state;
} gb28181_resource_filter;

typedef struct gb28181_live_request {
  const char* resource_id;
  int32_t mode;
} gb28181_live_request;

typedef struct gb28181_channel_live_request {
  const char* platform_id;
  const char* channel_gb_id;
  int32_t mode;
} gb28181_channel_live_request;

/* NULL options or timeout_ms == 0 use the default 10-second request timeout.
 * A cancellation handle is one-shot and may be shared across requests. */
typedef struct gb28181_request_options {
  uint32_t timeout_ms;
  const char* trace_id;
  const gb28181_cancel* cancellation;
} gb28181_request_options;

/* Callbacks run on the SDK callback executor. JSON and its strings are valid
 * only until the callback returns. Copy JSON to retain it. Completions carry
 * {code, message, data, trace_id}; json may be NULL on serialization failure.
 */
typedef void (*gb28181_completion_fn)(void* user_data,
                                      gb28181_status status,
                                      const char* json);
typedef void (*gb28181_event_fn)(void* user_data, const char* json);

GB28181_C_API uint32_t gb28181_abi_version(void);
GB28181_C_API const char* gb28181_error_message(gb28181_status status);
GB28181_C_API gb28181_status gb28181_config_init(gb28181_config* config);
GB28181_C_API gb28181_status
gb28181_platform_config_init(gb28181_platform_config* config);
GB28181_C_API gb28181_status
gb28181_resource_filter_init(gb28181_resource_filter* filter);

/* Start, stop and destroy are synchronous owner-thread operations. Do not
 * call them from a callback or concurrently with handle destruction. Stop
 * drains callbacks before returning. A successful destroy invalidates the
 * handle. On failure the caller retains it and may retry. NULL config uses
 * all SDK defaults. */
GB28181_C_API gb28181_status gb28181_client_create(gb28181_client** out_client);
GB28181_C_API gb28181_status gb28181_client_start(gb28181_client* client,
                                                  const gb28181_config* config);
GB28181_C_API gb28181_status gb28181_client_stop(gb28181_client* client);
GB28181_C_API gb28181_status gb28181_client_destroy(gb28181_client* client);

/* After destroying all clients, call this on the owner thread before
 * unloading the shared library or exiting its host. This terminal operation
 * is idempotent. No client may be started again in this library instance. */
GB28181_C_API gb28181_status gb28181_library_shutdown(void);

GB28181_C_API gb28181_status gb28181_cancel_create(gb28181_cancel** out_cancel);
GB28181_C_API void gb28181_cancel_request(gb28181_cancel* cancellation);
/* Destroy after submission is safe because accepted requests own a copy of
 * the cancellation state. Do not race destruction with submission or cancel. */
GB28181_C_API void gb28181_cancel_destroy(gb28181_cancel* cancellation);

/* For each asynchronous call, GB28181_OK means accepted. The completion is
 * delivered at most once, including timeout/cancellation; on synchronous
 * rejection there is no callback. Keep user_data alive until completion.
 * Never wait for another SDK callback from a callback. Timeout/cancellation
 * cannot undo a database mutation that has already committed. A NULL
 * resource filter uses the default filter. Catalog completion only reports
 * acceptance; use get_operation or subscribe_events for progress. */
GB28181_C_API gb28181_status
gb28181_add_platform(gb28181_client* client,
                     const gb28181_platform_config* platform,
                     const gb28181_request_options* options,
                     gb28181_completion_fn completion,
                     void* user_data);
GB28181_C_API gb28181_status
gb28181_update_platform(gb28181_client* client,
                        const gb28181_platform_config* platform,
                        const gb28181_request_options* options,
                        gb28181_completion_fn completion,
                        void* user_data);
GB28181_C_API gb28181_status
gb28181_remove_platform(gb28181_client* client,
                        const char* id,
                        const gb28181_request_options* options,
                        gb28181_completion_fn completion,
                        void* user_data);
GB28181_C_API gb28181_status
gb28181_get_platform(gb28181_client* client,
                     const char* id,
                     const gb28181_request_options* options,
                     gb28181_completion_fn completion,
                     void* user_data);
GB28181_C_API gb28181_status
gb28181_get_resource(gb28181_client* client,
                     const char* id,
                     const gb28181_request_options* options,
                     gb28181_completion_fn completion,
                     void* user_data);
GB28181_C_API gb28181_status
gb28181_sync_catalog(gb28181_client* client,
                     const char* id,
                     const gb28181_request_options* options,
                     gb28181_completion_fn completion,
                     void* user_data);
GB28181_C_API gb28181_status
gb28181_get_operation(gb28181_client* client,
                      const char* id,
                      const gb28181_request_options* options,
                      gb28181_completion_fn completion,
                      void* user_data);
GB28181_C_API gb28181_status
gb28181_get_device_info(gb28181_client* client,
                        const char* id,
                        const gb28181_request_options* options,
                        gb28181_completion_fn completion,
                        void* user_data);
GB28181_C_API gb28181_status
gb28181_get_device_status(gb28181_client* client,
                          const char* id,
                          const gb28181_request_options* options,
                          gb28181_completion_fn completion,
                          void* user_data);
GB28181_C_API gb28181_status
gb28181_refresh_device_info(gb28181_client* client,
                            const char* id,
                            const gb28181_request_options* options,
                            gb28181_completion_fn completion,
                            void* user_data);
GB28181_C_API gb28181_status
gb28181_refresh_device_status(gb28181_client* client,
                              const char* id,
                              const gb28181_request_options* options,
                              gb28181_completion_fn completion,
                              void* user_data);
GB28181_C_API gb28181_status
gb28181_get_playback(gb28181_client* client,
                     const char* id,
                     const gb28181_request_options* options,
                     gb28181_completion_fn completion,
                     void* user_data);
GB28181_C_API gb28181_status
gb28181_wait_playback_ready(gb28181_client* client,
                            const char* id,
                            const gb28181_request_options* options,
                            gb28181_completion_fn completion,
                            void* user_data);
GB28181_C_API gb28181_status
gb28181_renew_playback(gb28181_client* client,
                       const char* id,
                       const gb28181_request_options* options,
                       gb28181_completion_fn completion,
                       void* user_data);
GB28181_C_API gb28181_status
gb28181_release_playback(gb28181_client* client,
                         const char* id,
                         const gb28181_request_options* options,
                         gb28181_completion_fn completion,
                         void* user_data);
GB28181_C_API gb28181_status
gb28181_force_stop_playback(gb28181_client* client,
                            const char* id,
                            const gb28181_request_options* options,
                            gb28181_completion_fn completion,
                            void* user_data);
GB28181_C_API gb28181_status
gb28181_list_platforms(gb28181_client* client,
                       const gb28181_request_options* options,
                       gb28181_completion_fn completion,
                       void* user_data);
GB28181_C_API gb28181_status
gb28181_get_statistics(gb28181_client* client,
                       const gb28181_request_options* options,
                       gb28181_completion_fn completion,
                       void* user_data);
GB28181_C_API gb28181_status
gb28181_list_resources(gb28181_client* client,
                       const gb28181_resource_filter* filter,
                       const gb28181_request_options* options,
                       gb28181_completion_fn completion,
                       void* user_data);
GB28181_C_API gb28181_status
gb28181_start_live(gb28181_client* client,
                   const gb28181_live_request* request,
                   const gb28181_request_options* options,
                   gb28181_completion_fn completion,
                   void* user_data);

/* Prefer this entry point for new integrations. Use GB28181_MODE_DEFAULT to
 * inherit the platform receive mode. The callback returns a playback lease. */
GB28181_C_API gb28181_status
gb28181_start_live_by_channel(gb28181_client* client,
                              const gb28181_channel_live_request* request,
                              const gb28181_request_options* options,
                              gb28181_completion_fn completion,
                              void* user_data);

/* Enables reader-based retention. Zero selects the default 60 seconds.
 * Returns the current playback snapshot. Poll get_playback or wait for
 * readiness if endpoints are still empty. */
GB28181_C_API gb28181_status
gb28181_get_playback_address(gb28181_client* client,
                             const char* playback_id,
                             uint32_t idle_timeout_seconds,
                             const gb28181_request_options* options,
                             gb28181_completion_fn completion,
                             void* user_data);

/* Events use the SDK event JSON shape and may include catalog_batch. A
 * running callback can finish after unsubscribe; keep its user_data alive
 * until stop returns or the callback has otherwise finished. */
GB28181_C_API gb28181_status
gb28181_subscribe_events(gb28181_client* client,
                         gb28181_event_fn callback,
                         void* user_data,
                         uint64_t* out_subscription);
GB28181_C_API gb28181_status gb28181_unsubscribe_events(gb28181_client* client,
                                                        uint64_t subscription);

#ifdef __cplusplus
}
#endif

#undef GB28181_C_API

#endif /* GB28181_INCLUDE_GB28181_C_API_H_ */
