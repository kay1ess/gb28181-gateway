ALTER TABLE platforms ADD COLUMN default_receive_mode INTEGER
  NOT NULL DEFAULT 0 CHECK(default_receive_mode IN (0,1,2));
PRAGMA user_version=2;
