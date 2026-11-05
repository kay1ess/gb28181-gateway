#include "src/application/catalog_accumulator.h"

#include <tuple>

namespace gb28181 {
namespace {
auto Fields(const Resource& item) {
  return std::tie(item.gb_id, item.parent_gb_id, item.name, item.manufacturer,
                  item.model, item.address, item.civil_code, item.parental,
                  item.state);
}
}  // namespace

Result<bool> CatalogAccumulator::Add(uint32_t expected,
                                     const std::vector<Resource>& batch) {
  if (failed_)
    return Error{
        ErrorCode::kCatalogSyncFailed, "Catalog task already failed", {}};
  auto fail = [this](const char* message) -> Result<bool> {
    failed_ = true;
    return Error{ErrorCode::kCatalogSyncFailed, message, {}};
  };
  if (expected > 100000 || (expected_ && *expected_ != expected))
    return fail("Inconsistent catalog total");
  expected_ = expected;
  for (const auto& item : batch) {
    if (item.gb_id.empty())
      return fail("Empty catalog identity");
    auto found = resources_.find(item.gb_id);
    if (found != resources_.end()) {
      if (Fields(found->second) != Fields(item))
        return fail("Conflicting duplicate catalog item");
    } else {
      resources_.emplace(item.gb_id, item);
    }
  }
  if (resources_.size() > expected)
    return fail("Catalog exceeds declared total");
  return resources_.size() == expected;
}

std::vector<Resource> CatalogAccumulator::Snapshot() const {
  std::vector<Resource> result;
  for (const auto& item : resources_)
    result.push_back(item.second);
  return result;
}
}  // namespace gb28181
