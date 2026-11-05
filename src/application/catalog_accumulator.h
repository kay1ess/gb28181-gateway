#ifndef GB28181_SRC_APPLICATION_CATALOG_ACCUMULATOR_H_
#define GB28181_SRC_APPLICATION_CATALOG_ACCUMULATOR_H_
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "gb28181/result.h"
#include "gb28181/types.h"

namespace gb28181 {
// One accumulator per (platform, command, SN); never share across requests.
class CatalogAccumulator {
 public:
  Result<bool> Add(uint32_t expected, const std::vector<Resource>& batch);

  uint32_t received() const {
    return static_cast<uint32_t>(resources_.size());
  }

  uint32_t expected() const {
    return expected_.value_or(0);
  }

  std::vector<Resource> Snapshot() const;

 private:
  bool failed_ = false;
  std::optional<uint32_t> expected_;
  std::map<std::string, Resource> resources_;
};
}  // namespace gb28181
#endif  // GB28181_SRC_APPLICATION_CATALOG_ACCUMULATOR_H_
