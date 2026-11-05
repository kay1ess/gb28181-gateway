#ifndef GB28181_SRC_APPLICATION_APPLICATION_H_
#define GB28181_SRC_APPLICATION_APPLICATION_H_
#include <memory>

#include "gb28181/client.h"
#include "src/ports/media_service.h"
#include "src/ports/repositories.h"
#include "src/ports/sip_service.h"

namespace gb28181 {
// The composition root supplies adapters. No third-party implementation is
// referenced by the application target or by the public SDK interface.
std::unique_ptr<Client> CreateApplication(
    std::unique_ptr<IRepository> repository,
    std::unique_ptr<IGb28181Service> sip,
    std::unique_ptr<IMediaService> media);
}  // namespace gb28181
#endif  // GB28181_SRC_APPLICATION_APPLICATION_H_
