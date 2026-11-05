#include "gb28181/client.h"

#include <memory>

#include "src/adapters/media/zlm_service.h"
#include "src/adapters/sip/resip_service.h"
#include "src/adapters/storage/sqlite/repository.h"
#include "src/application/application.h"

namespace gb28181 {
std::unique_ptr<Client> CreateClient() {
  return CreateApplication(std::make_unique<storage::SqliteRepository>(),
                           std::make_unique<sip::ResipService>(),
                           std::make_unique<media::ZlmMediaService>());
}
}  // namespace gb28181
