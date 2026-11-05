#ifndef GB28181_SRC_PORTS_REPOSITORIES_H_
#define GB28181_SRC_PORTS_REPOSITORIES_H_
#include <vector>

#include "gb28181/result.h"
#include "gb28181/types.h"

namespace gb28181 {
class IPlatformRepository {
 public:
  virtual ~IPlatformRepository() = default;
  virtual Result<void> SavePlatform(const PlatformConfig& platform,
                                    bool create) = 0;
  virtual Result<PlatformConfig> FindPlatform(const PlatformId& id) = 0;
  virtual Result<std::vector<PlatformConfig>> ListPlatforms() = 0;
  virtual Result<void> RemovePlatform(const PlatformId& id) = 0;
};

class IResourceRepository {
 public:
  virtual ~IResourceRepository() = default;
  virtual Result<Resource> FindResource(const ResourceId& id) = 0;
  virtual Result<std::vector<Resource>> ListResources(
      const ResourceFilter& filter) = 0;
  // Called only with a fully aggregated catalog. A failure must preserve the
  // old catalog, including when an INSERT fails after some valid rows.
  virtual Result<void> ReplaceCatalog(
      const PlatformId& platform_id,
      const std::vector<Resource>& resources) = 0;
  virtual Result<void> SaveDeviceInfo(const DeviceInfo& info) = 0;
  virtual Result<DeviceInfo> FindDeviceInfo(const ResourceId& id) = 0;
  virtual Result<void> SaveDeviceStatus(const DeviceStatus& status) = 0;
  virtual Result<DeviceStatus> FindDeviceStatus(const ResourceId& id) = 0;
};

class IRepository : public IPlatformRepository, public IResourceRepository {
 public:
  virtual Result<void> Open(const std::string& path) = 0;
  virtual void Close() = 0;
};
}  // namespace gb28181
#endif  // GB28181_SRC_PORTS_REPOSITORIES_H_
