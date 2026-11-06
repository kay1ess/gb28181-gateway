#include "gb28181/c_api.h"

#include <stdio.h>
#include <string.h>

static void UnexpectedCompletion(void* user_data,
                                 gb28181_status status,
                                 const char* json) {
  int* called = (int*)user_data;
  (void)status;
  (void)json;
  *called = 1;
}

int main(void) {
  gb28181_config config;
  gb28181_client* client = NULL;
  gb28181_cancel* cancellation = NULL;
  const gb28181_channel_live_request live = {"lower_1", "34020000001320000001",
                                             GB28181_MODE_DEFAULT};
  int called = 0;
  if (gb28181_abi_version() != 1 ||
      gb28181_config_init(&config) != GB28181_OK || config.rtp_port != 10000 ||
      gb28181_client_create(&client) != GB28181_OK ||
      gb28181_cancel_create(&cancellation) != GB28181_OK) {
    return 1;
  }
  gb28181_cancel_request(cancellation);
  gb28181_cancel_destroy(cancellation);
  config.sip_port = 0;
  if (gb28181_client_start(client, &config) != GB28181_INVALID_ARGUMENT ||
      gb28181_list_platforms(client, NULL, UnexpectedCompletion, &called) !=
          GB28181_INVALID_STATE ||
      gb28181_start_live_by_channel(client, &live, NULL, UnexpectedCompletion,
                                    &called) != GB28181_INVALID_STATE ||
      gb28181_start_live_by_channel(client, NULL, NULL, UnexpectedCompletion,
                                    &called) != GB28181_INVALID_ARGUMENT ||
      gb28181_get_playback_address(client, "play", 0, NULL,
                                   UnexpectedCompletion,
                                   &called) != GB28181_INVALID_STATE ||
      strcmp(gb28181_error_message(GB28181_OK), "Success") != 0 ||
      gb28181_client_destroy(client) != GB28181_OK ||
      gb28181_library_shutdown() != GB28181_OK ||
      gb28181_library_shutdown() != GB28181_OK || called) {
    return 1;
  }
  puts("C SDK ABI smoke passed");
  return 0;
}
