CREATE TABLE platforms(
  id TEXT PRIMARY KEY NOT NULL,
  gb_id TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  realm TEXT NOT NULL,
  username TEXT NOT NULL,
  password TEXT NOT NULL,
  enabled INTEGER NOT NULL,
  heartbeat_timeout INTEGER NOT NULL CHECK(heartbeat_timeout > 0)
);
CREATE TABLE resources(
  id TEXT PRIMARY KEY NOT NULL,
  platform_id TEXT NOT NULL REFERENCES platforms(id) ON DELETE CASCADE,
  gb_id TEXT NOT NULL,
  parent_gb_id TEXT NOT NULL,
  name TEXT NOT NULL,
  manufacturer TEXT NOT NULL,
  model TEXT NOT NULL,
  address TEXT NOT NULL,
  civil_code TEXT NOT NULL,
  parental INTEGER NOT NULL,
  state INTEGER NOT NULL,
  updated_ms INTEGER NOT NULL,
  UNIQUE(platform_id, gb_id)
);
CREATE INDEX resources_parent ON resources(platform_id, parent_gb_id);
CREATE TABLE device_info(
  resource_id TEXT PRIMARY KEY REFERENCES resources(id) ON DELETE CASCADE,
  name TEXT NOT NULL, manufacturer TEXT NOT NULL, model TEXT NOT NULL,
  firmware TEXT NOT NULL, queried_ms INTEGER NOT NULL
);
CREATE TABLE device_status(
  resource_id TEXT PRIMARY KEY REFERENCES resources(id) ON DELETE CASCADE,
  state INTEGER NOT NULL, status TEXT NOT NULL, queried_ms INTEGER NOT NULL
);
PRAGMA user_version=1;
