#ifndef GB28181_APPS_SERVER_HTTP_SERVER_H_
#define GB28181_APPS_SERVER_HTTP_SERVER_H_
#include <memory>
#include <string>

#include "gb28181/client.h"

namespace gb28181::http {
void RegisterRoutes(std::shared_ptr<Client> client, const std::string& token);
}  // namespace gb28181::http
#endif  // GB28181_APPS_SERVER_HTTP_SERVER_H_
