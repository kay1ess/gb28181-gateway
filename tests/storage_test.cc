#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "src/adapters/storage/sqlite/repository.h"
#include "src/adapters/storage/sqlite/schema.h"
#include "tests/test_support.h"

namespace {
using namespace gb28181;
using test::Require;
using test::RequireOk;

Resource MakeResource(const std::string& id,
                      const std::string& platform,
                      const std::string& gb_id) {
  Resource resource;
  resource.id = id;
  resource.platform_id = platform;
  resource.gb_id = gb_id;
  resource.name = "camera ' 中文 ? %";
  resource.state = OnlineState::kOnline;
  resource.updated_ms = 1234;
  return resource;
}

void TestRepository(const std::filesystem::path& path) {
  storage::SqliteRepository store;
  RequireOk(store.Open(path.string()), "open and migrate");
  PlatformConfig first;
  first.id = "p1";
  first.gb_id = "34020000002000000002";
  first.password = "password' ; DROP TABLE resources; --";
  RequireOk(store.SavePlatform(first, true), "save first platform");
  auto duplicate = store.SavePlatform(first, true);
  Require(
      !duplicate.ok() && duplicate.error().code == ErrorCode::kAlreadyExists,
      "duplicate platform classified");
  PlatformConfig second = first;
  second.id = "p2";
  second.gb_id = "34020000002000000003";
  RequireOk(store.SavePlatform(second, true), "save second platform");
  Require(store.FindPlatform(first.id).value().password == first.password,
          "bound SQL values preserved");
  auto a = MakeResource("p1/a", "p1", "camera-a");
  auto b = MakeResource("p1/b", "p1", "camera-b");
  RequireOk(store.ReplaceCatalog("p1", {a, b}), "initial catalog");
  RequireOk(
      store.ReplaceCatalog("p2", {MakeResource("p2/a", "p2", "camera-a")}),
      "same GB code through different sharing source");
  Require(store.ListResources({}).value().size() == 3,
          "sharing source retained");
  Require(
      store.FindResourceByChannel("p1", "camera-a").value().id == "p1/a" &&
          store.FindResourceByChannel("p2", "camera-a").value().id == "p2/a",
      "channel lookup uses platform and preserves opaque resource IDs");
  auto missing_channel = store.FindResourceByChannel("p2", "camera-b");
  Require(
      !missing_channel && missing_channel.error().code == ErrorCode::kNotFound,
      "channel lookup does not fall back to another platform");
  DeviceInfo info{a.id, "name", "vendor", "model", "v1", 42};
  RequireOk(store.SaveDeviceInfo(info), "device info cache");
  RequireOk(store.SaveDeviceStatus({a.id, OnlineState::kOnline, "OK", 43}),
            "device status cache");
  RequireOk(store.ReplaceCatalog("p1", {a, b}), "refresh keeps caches");
  Require(store.FindDeviceInfo(a.id).value().firmware == "v1",
          "cache survives catalog refresh");
  auto inserted = MakeResource("p1/c", "p1", "camera-c");
  auto conflict = MakeResource("p1/d", "p1", "camera-a");
  Require(!store.ReplaceCatalog("p1", {inserted, conflict}).ok(),
          "mid-batch SQL uniqueness failure");
  Require(!store.FindResource(inserted.id).ok(), "partial insert rolled back");
  RequireOk(store.FindResource(b.id), "old catalog survives failed refresh");
  auto wrong_source = MakeResource("p2/a", "p1", "camera-d");
  Require(!store.ReplaceCatalog("p1", {inserted, wrong_source}).ok(),
          "source identity conflict rejected");
  Require(!store.FindResource(inserted.id).ok(),
          "early return rolls back transaction");
  ResourceFilter filter;
  filter.keyword = "中文";
  filter.limit = 1;
  Require(store.ListResources(filter).value().size() == 1,
          "Unicode search and pagination");
  filter.state = OnlineState::kOffline;
  Require(store.ListResources(filter).value().empty(),
          "resource state filter excludes online channels");
  filter.state = OnlineState::kOnline;
  Require(store.ListResources(filter).value().size() == 1,
          "state filter composes with search and pagination");
  RequireOk(store.ReplaceCatalog("p1", {a}),
            "complete snapshot prunes stale resource");
  Require(!store.FindResource(b.id).ok(), "stale resource pruned");
  {
    storage::Database competing;
    competing.Open(path.string());
    storage::Transaction transaction(competing);
    first.name = "busy";
    auto busy = store.SavePlatform(first, false);
    Require(!busy.ok() && busy.error().code == ErrorCode::kStorageBusy,
            "writer lock maps to busy");
  }
  store.Close();
  RequireOk(store.Open(path.string()), "reopen existing schema");
  Require(store.FindResource(a.id).value().state == OnlineState::kUnknown,
          "restart invalidates liveness");
  Require(store.FindDeviceInfo(a.id).value().queried_ms == 42,
          "cached information keeps freshness timestamp");
  RequireOk(store.ReplaceCatalog("p1", {}), "empty complete catalog");
  Require(!store.FindDeviceInfo(a.id).ok(),
          "resource deletion cascades cached info");
  Require(store.ListResources({}).value().size() == 1,
          "other platform catalog survives pruning");
  std::vector<Resource> large;
  for (int index = 0; index < 1000; ++index)
    large.push_back(
        MakeResource("p1/benchmark_" + std::to_string(index), "p1",
                     std::to_string(10000000000000000000ULL + index)));
  const auto began = std::chrono::steady_clock::now();
  RequireOk(store.ReplaceCatalog("p1", large), "1000-row catalog transaction");
  const auto committed = std::chrono::steady_clock::now();
  ResourceFilter large_filter;
  large_filter.platform_id = "p1";
  large_filter.limit = 1000;
  Require(store.ListResources(large_filter).value().size() == 1000,
          "1000-row storage query");
  std::cout << "1000-row SQLite commit us="
            << std::chrono::duration_cast<std::chrono::microseconds>(committed -
                                                                     began)
                   .count()
            << " list us="
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now() - committed)
                   .count()
            << std::endl;
  RequireOk(store.ReplaceCatalog("p1", {}), "remove benchmark catalog");
  RequireOk(store.RemovePlatform("p2"), "remove platform");
  Require(store.ListResources({}).value().empty(),
          "platform deletion cascades resources");
  store.Close();
  {
    storage::Database future;
    future.Open(path.string());
    future.Execute("PRAGMA user_version=999");
  }
  auto newer = store.Open(path.string());
  Require(
      !newer.ok() && newer.error().code == ErrorCode::kStorageMigrationFailed,
      "future schema refused");
}
}  // namespace

int main() {
  const char* temp = std::getenv("TEST_TMPDIR");
  const auto directory = temp ? std::filesystem::path(temp)
                              : std::filesystem::temp_directory_path();
  const auto path = directory / "repository-test.db";
  const auto legacy_path = directory / "legacy-v1.db";
  {
    storage::Database legacy;
    legacy.Open(legacy_path.string());
    legacy.Execute(storage::kInitialSchema);
    legacy.Execute(
        "INSERT INTO platforms VALUES('legacy','34020000002000000009',"
        "'old name','3402000000','','old password',1,180);"
        "INSERT INTO resources VALUES('legacy:channel','legacy','channel',"
        "'','camera','','','','',0,1,123);");
  }
  {
    storage::SqliteRepository migrated;
    RequireOk(migrated.Open(legacy_path.string()), "migrate version 1");
    auto platform = migrated.FindPlatform("legacy").value();
    Require(platform.default_receive_mode == RtpReceiveMode::kUdp &&
                platform.password == "old password",
            "migration defaults to UDP and preserves credentials");
    Require(
        migrated.FindResource("legacy:channel").value().platform_id == "legacy",
        "migration preserves resource association");
    platform.default_receive_mode = RtpReceiveMode::kTcpActive;
    RequireOk(migrated.SavePlatform(platform, false), "save platform mode");
    migrated.Close();
    RequireOk(migrated.Open(legacy_path.string()), "reopen migrated database");
    Require(migrated.FindPlatform("legacy").value().default_receive_mode ==
                RtpReceiveMode::kTcpActive,
            "platform mode survives restart");
    platform.default_receive_mode = static_cast<RtpReceiveMode>(99);
    Require(!migrated.SavePlatform(platform, false).ok(),
            "invalid mode rejected");
  }
  TestRepository(path);
  storage::SqliteRepository invalid;
  Require(
      !invalid.Open((directory / "missing-parent" / "file.db").string()).ok(),
      "invalid database path rejected");
#ifdef _WIN32
  const auto read_only = directory / "read-only.db";
  {
    storage::SqliteRepository writable;
    RequireOk(writable.Open(read_only.string()), "create read-only fixture");
  }
  std::filesystem::permissions(read_only, std::filesystem::perms::owner_read,
                               std::filesystem::perm_options::replace);
  const auto denied = invalid.Open(read_only.string());
  std::filesystem::permissions(read_only, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
#else
  // procfs is read-only even when container tests run as root; chmod on a
  // temporary database would not prove failure for a privileged process.
  const auto denied = invalid.Open("/proc/version");
#endif
  Require(
      !denied.ok() && denied.error().code == ErrorCode::kStorageMigrationFailed,
      "read-only database path cannot report successful initialization");
  std::cout << "storage tests passed\n";
}
