#ifndef GB28181_SRC_ADAPTERS_STORAGE_SQLITE_REPOSITORY_H_
#define GB28181_SRC_ADAPTERS_STORAGE_SQLITE_REPOSITORY_H_
#include <mutex>

#include "src/adapters/storage/sqlite/database.h"
#include "src/ports/repositories.h"

namespace gb28181::storage {
// Synchronous adapter: invoke from a storage executor, not SIP/media threads.
// Each method holds the connection lock for its complete transaction.
class SqliteRepository final : public IRepository {
 public:
  Result<void> Open(const std::string& path) override;
  void Close() override;
  Result<void> SavePlatform(const PlatformConfig& platform,
                            bool create) override;
  Result<PlatformConfig> FindPlatform(const PlatformId& id) override;
  Result<std::vector<PlatformConfig>> ListPlatforms() override;
  Result<void> RemovePlatform(const PlatformId& id) override;
  Result<Resource> FindResource(const ResourceId& id) override;
  Result<std::vector<Resource>> ListResources(
      const ResourceFilter& filter) override;
  Result<void> ReplaceCatalog(const PlatformId& id,
                              const std::vector<Resource>& resources) override;
  Result<void> SaveDeviceInfo(const DeviceInfo& info) override;
  Result<DeviceInfo> FindDeviceInfo(const ResourceId& id) override;
  Result<void> SaveDeviceStatus(const DeviceStatus& status) override;
  Result<DeviceStatus> FindDeviceStatus(const ResourceId& id) override;

 private:
  std::mutex mutex_;
  Database database_;
};
}  // namespace gb28181::storage
#endif  // GB28181_SRC_ADAPTERS_STORAGE_SQLITE_REPOSITORY_H_
