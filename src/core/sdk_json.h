#ifndef GB28181_SRC_CORE_SDK_JSON_H_
#define GB28181_SRC_CORE_SDK_JSON_H_

#include <string>
#include <vector>

#include <json/json.h>

#include "gb28181/events.h"
#include "gb28181/types.h"

namespace gb28181 {
std::string PublicMessage(const Error& error);
Json::Value Serialize(const Error& value);
Json::Value Serialize(const Platform& value);
Json::Value Serialize(const Resource& value);
Json::Value Serialize(const Operation& value);
Json::Value Serialize(const DeviceInfo& value);
Json::Value Serialize(const DeviceStatus& value);
Json::Value Serialize(const Playback& value);
Json::Value Serialize(const PlaybackLease& value);
Json::Value Serialize(const Statistics& value);
Json::Value Serialize(const Event& value);

template <typename T>
Json::Value Serialize(const std::vector<T>& values) {
  Json::Value result(Json::arrayValue);
  for (const auto& value : values)
    result.append(Serialize(value));
  return result;
}
}  // namespace gb28181

#endif  // GB28181_SRC_CORE_SDK_JSON_H_
