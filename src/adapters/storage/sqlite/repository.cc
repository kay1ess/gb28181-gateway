#include "src/adapters/storage/sqlite/repository.h"

#include <set>
#include <utility>

#include "src/adapters/storage/sqlite/receive_mode_schema.h"
#include "src/adapters/storage/sqlite/schema.h"

namespace gb28181::storage {
namespace {
Error Missing() {
  return {ErrorCode::kNotFound, "Not found", {}};
}

PlatformConfig ReadPlatform(Statement& row) {
  PlatformConfig value;
  value.id = row.Text(0);
  value.gb_id = row.Text(1);
  value.name = row.Text(2);
  value.realm = row.Text(3);
  value.username = row.Text(4);
  value.password = row.Text(5);
  value.enabled = row.Integer(6) != 0;
  value.heartbeat_timeout_seconds = static_cast<uint32_t>(row.Integer(7));
  value.default_receive_mode = static_cast<RtpReceiveMode>(row.Integer(8));
  return value;
}

Resource ReadResource(Statement& row) {
  Resource value;
  value.id = row.Text(0);
  value.platform_id = row.Text(1);
  value.gb_id = row.Text(2);
  value.parent_gb_id = row.Text(3);
  value.name = row.Text(4);
  value.manufacturer = row.Text(5);
  value.model = row.Text(6);
  value.address = row.Text(7);
  value.civil_code = row.Text(8);
  value.parental = row.Integer(9) != 0;
  value.state = static_cast<OnlineState>(row.Integer(10));
  value.updated_ms = row.Integer(11);
  return value;
}
}  // namespace

Result<void> SqliteRepository::Open(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (database_.handle()) {
    return Error{ErrorCode::kInvalidState, "Repository already open", {}};
  }
  try {
    database_.Open(path);
    int64_t version = 0;
    {
      Statement query(database_, "PRAGMA user_version");
      query.Step();
      version = query.Integer(0);
    }
    if (version > 2) {
      database_.Close();
      return Error{ErrorCode::kStorageMigrationFailed,
                   "Database schema is newer than this SDK",
                   {}};
    }
    if (version == 0) {
      Transaction transaction(database_);
      database_.Execute(kInitialSchema);
      transaction.Commit();
      version = 1;
    }
    if (version == 1) {
      Transaction transaction(database_);
      database_.Execute(kReceiveModeSchema);
      transaction.Commit();
    }
    // All persisted liveness is stale after a process restart.
    database_.Execute(
        "UPDATE resources SET state=0; UPDATE device_status SET state=0");
    return {};
  } catch (const DatabaseError& error) {
    database_.Close();
    auto translated = TranslateError(error, true);
    if (translated.code != ErrorCode::kStorageBusy) {
      translated.code = ErrorCode::kStorageMigrationFailed;
      translated.message = ErrorMessage(translated.code);
    }
    return translated;
  }
}

void SqliteRepository::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  database_.Close();
}

Result<void> SqliteRepository::SavePlatform(const PlatformConfig& platform,
                                            bool create) {
  if (platform.id.empty() || platform.gb_id.empty() ||
      platform.heartbeat_timeout_seconds == 0 ||
      (platform.default_receive_mode != RtpReceiveMode::kUdp &&
       platform.default_receive_mode != RtpReceiveMode::kTcpPassive &&
       platform.default_receive_mode != RtpReceiveMode::kTcpActive))
    return Error{
        ErrorCode::kInvalidArgument, "Invalid platform configuration", {}};
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(
        database_,
        create ? "INSERT INTO platforms(id,gb_id,name,realm,username,"
                 "password,enabled,heartbeat_timeout,default_receive_mode) "
                 "VALUES(?,?,?,?,?,?,?,?,?)"
               : "UPDATE platforms SET "
                 "gb_id=?2,name=?3,realm=?4,username=?5,password=?6,"
                 "enabled=?7,heartbeat_timeout=?8,"
                 "default_receive_mode=?9 WHERE id=?1");
    statement.Bind(1, platform.id);
    statement.Bind(2, platform.gb_id);
    statement.Bind(3, platform.name);
    statement.Bind(4, platform.realm);
    statement.Bind(5, platform.username);
    statement.Bind(6, platform.password);
    statement.Bind(7, static_cast<int64_t>(platform.enabled));
    statement.Bind(8, platform.heartbeat_timeout_seconds);
    statement.Bind(9, static_cast<int64_t>(platform.default_receive_mode));
    statement.Step();
    if (!create && !database_.Changes())
      return Missing();
    return {};
  } catch (const DatabaseError& error) {
    return TranslateError(error, true);
  }
}

Result<PlatformConfig> SqliteRepository::FindPlatform(const PlatformId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(database_, "SELECT * FROM platforms WHERE id=?");
    statement.Bind(1, id);
    if (!statement.Step())
      return Missing();
    return ReadPlatform(statement);
  } catch (const DatabaseError& error) {
    return TranslateError(error, false);
  }
}

Result<std::vector<PlatformConfig>> SqliteRepository::ListPlatforms() {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(database_, "SELECT * FROM platforms ORDER BY id");
    std::vector<PlatformConfig> rows;
    while (statement.Step())
      rows.push_back(ReadPlatform(statement));
    return rows;
  } catch (const DatabaseError& error) {
    return TranslateError(error, false);
  }
}

Result<void> SqliteRepository::RemovePlatform(const PlatformId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(database_, "DELETE FROM platforms WHERE id=?");
    statement.Bind(1, id);
    statement.Step();
    if (!database_.Changes())
      return Missing();
    return {};
  } catch (const DatabaseError& error) {
    return TranslateError(error, true);
  }
}

Result<Resource> SqliteRepository::FindResource(const ResourceId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(database_, "SELECT * FROM resources WHERE id=?");
    statement.Bind(1, id);
    if (!statement.Step())
      return Missing();
    return ReadResource(statement);
  } catch (const DatabaseError& error) {
    return TranslateError(error, false);
  }
}

Result<std::vector<Resource>> SqliteRepository::ListResources(
    const ResourceFilter& filter) {
  if (filter.limit == 0 || filter.limit > 1000)
    return Error{
        ErrorCode::kInvalidArgument, "Limit must be between 1 and 1000", {}};
  if (filter.state && *filter.state != OnlineState::kUnknown &&
      *filter.state != OnlineState::kOnline &&
      *filter.state != OnlineState::kOffline)
    return Error{ErrorCode::kInvalidArgument, "Invalid resource state", {}};
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(database_,
                        "SELECT * FROM resources WHERE (?1='' OR "
                        "platform_id=?1) AND (?2='' OR instr(name,?2)>0 OR "
                        "instr(gb_id,?2)>0) AND (?5<0 OR state=?5) "
                        "ORDER BY id LIMIT ?3 OFFSET ?4");
    statement.Bind(1, filter.platform_id);
    statement.Bind(2, filter.keyword);
    statement.Bind(3, filter.limit);
    statement.Bind(4, filter.offset);
    statement.Bind(5, filter.state ? static_cast<int64_t>(*filter.state) : -1);
    std::vector<Resource> rows;
    while (statement.Step())
      rows.push_back(ReadResource(statement));
    return rows;
  } catch (const DatabaseError& error) {
    return TranslateError(error, false);
  }
}

Result<void> SqliteRepository::ReplaceCatalog(
    const PlatformId& id,
    const std::vector<Resource>& resources) {
  std::set<std::string> gb_ids;
  for (const auto& resource : resources) {
    if (resource.platform_id != id || resource.id.empty() ||
        resource.gb_id.empty() || !gb_ids.insert(resource.gb_id).second)
      return Error{ErrorCode::kInvalidArgument, "Invalid catalog batch", {}};
  }
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Transaction transaction(database_);
    Statement platform(database_, "SELECT 1 FROM platforms WHERE id=?");
    platform.Bind(1, id);
    if (!platform.Step())
      return Missing();
    // Keep existing rows and their query caches; prune only after all inserts
    // succeed. Temporary membership is scoped to this single transaction.
    database_.Execute(
        "CREATE TEMP TABLE IF NOT EXISTS catalog_members(id TEXT PRIMARY KEY); "
        "DELETE FROM catalog_members");
    Statement member(database_, "INSERT INTO catalog_members VALUES(?)");
    Statement upsert(
        database_,
        "INSERT INTO resources VALUES(?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) "
        "DO UPDATE SET "
        "parent_gb_id=excluded.parent_gb_id,name=excluded.name,manufacturer="
        "excluded.manufacturer,model=excluded.model,address=excluded.address,"
        "civil_code=excluded.civil_code,parental=excluded.parental,state="
        "excluded.state,updated_ms=excluded.updated_ms");
    for (const auto& resource : resources) {
      // Reject a caller accidentally reusing another sharing source's ID.
      Statement existing(database_,
                         "SELECT platform_id,gb_id FROM resources WHERE id=?");
      existing.Bind(1, resource.id);
      if (existing.Step() &&
          (existing.Text(0) != id || existing.Text(1) != resource.gb_id))
        return Error{ErrorCode::kInvalidArgument,
                     "Resource identity conflicts with another catalog",
                     {}};
      member.Bind(1, resource.id);
      member.Step();
      member.Reset();
      upsert.Bind(1, resource.id);
      upsert.Bind(2, id);
      upsert.Bind(3, resource.gb_id);
      upsert.Bind(4, resource.parent_gb_id);
      upsert.Bind(5, resource.name);
      upsert.Bind(6, resource.manufacturer);
      upsert.Bind(7, resource.model);
      upsert.Bind(8, resource.address);
      upsert.Bind(9, resource.civil_code);
      upsert.Bind(10, static_cast<int64_t>(resource.parental));
      upsert.Bind(11, static_cast<int64_t>(resource.state));
      upsert.Bind(12, resource.updated_ms);
      upsert.Step();
      upsert.Reset();
    }
    Statement prune(database_,
                    "DELETE FROM resources WHERE platform_id=? AND id NOT IN "
                    "(SELECT id FROM catalog_members)");
    prune.Bind(1, id);
    prune.Step();
    transaction.Commit();
    return {};
  } catch (const DatabaseError& error) {
    return TranslateError(error, true);
  }
}

Result<void> SqliteRepository::SaveDeviceInfo(const DeviceInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(
        database_,
        "INSERT INTO device_info VALUES(?,?,?,?,?,?) ON CONFLICT(resource_id) "
        "DO UPDATE SET "
        "name=excluded.name,manufacturer=excluded.manufacturer,model=excluded."
        "model,firmware=excluded.firmware,queried_ms=excluded.queried_ms");
    statement.Bind(1, info.resource_id);
    statement.Bind(2, info.name);
    statement.Bind(3, info.manufacturer);
    statement.Bind(4, info.model);
    statement.Bind(5, info.firmware);
    statement.Bind(6, info.queried_ms);
    statement.Step();
    return {};
  } catch (const DatabaseError& error) {
    return TranslateError(error, true);
  }
}

Result<DeviceInfo> SqliteRepository::FindDeviceInfo(const ResourceId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(database_,
                        "SELECT * FROM device_info WHERE resource_id=?");
    statement.Bind(1, id);
    if (!statement.Step())
      return Missing();
    return DeviceInfo{statement.Text(0), statement.Text(1),
                      statement.Text(2), statement.Text(3),
                      statement.Text(4), statement.Integer(5)};
  } catch (const DatabaseError& error) {
    return TranslateError(error, false);
  }
}

Result<void> SqliteRepository::SaveDeviceStatus(const DeviceStatus& status) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Transaction transaction(database_);
    Statement statement(database_,
                        "INSERT INTO device_status VALUES(?,?,?,?) ON "
                        "CONFLICT(resource_id) DO UPDATE SET "
                        "state=excluded.state,status=excluded.status,queried_"
                        "ms=excluded.queried_ms");
    statement.Bind(1, status.resource_id);
    statement.Bind(2, static_cast<int64_t>(status.state));
    statement.Bind(3, status.status);
    statement.Bind(4, status.queried_ms);
    statement.Step();
    Statement resource(database_,
                       "UPDATE resources SET state=?,updated_ms=? WHERE id=?");
    resource.Bind(1, static_cast<int64_t>(status.state));
    resource.Bind(2, status.queried_ms);
    resource.Bind(3, status.resource_id);
    resource.Step();
    transaction.Commit();
    return {};
  } catch (const DatabaseError& error) {
    return TranslateError(error, true);
  }
}

Result<DeviceStatus> SqliteRepository::FindDeviceStatus(const ResourceId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    Statement statement(database_,
                        "SELECT * FROM device_status WHERE resource_id=?");
    statement.Bind(1, id);
    if (!statement.Step())
      return Missing();
    return DeviceStatus{statement.Text(0),
                        static_cast<OnlineState>(statement.Integer(1)),
                        statement.Text(2), statement.Integer(3)};
  } catch (const DatabaseError& error) {
    return TranslateError(error, false);
  }
}
}  // namespace gb28181::storage
