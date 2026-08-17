CREATE TABLE readings (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id    TEXT    NOT NULL,
    moisture_pct REAL    NOT NULL CHECK (moisture_pct >= 0 AND moisture_pct <= 100),
    timestamp    INTEGER NOT NULL,
    received_at  INTEGER NOT NULL
);

CREATE INDEX idx_readings_device_timestamp ON readings(device_id, timestamp);

CREATE TABLE watering_events (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id               TEXT    NOT NULL,
    request_id              TEXT,
    trigger_type            TEXT    NOT NULL CHECK (trigger_type IN ('moisture', 'schedule', 'manual')),
    status                  TEXT    NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'completed', 'failed', 'skipped')),
    requested_duration_sec  INTEGER NOT NULL,
    actual_duration_sec     INTEGER,
    moisture_before_pct     REAL CHECK (moisture_before_pct IS NULL OR (moisture_before_pct >= 0 AND moisture_before_pct <= 100)),
    moisture_after_pct      REAL CHECK (moisture_after_pct IS NULL OR (moisture_after_pct >= 0 AND moisture_after_pct <= 100)),
    started_at              INTEGER NOT NULL,
    completed_at            INTEGER
);

CREATE INDEX idx_watering_events_device_started ON watering_events(device_id, started_at);
CREATE UNIQUE INDEX idx_watering_events_request_id ON watering_events(request_id) WHERE request_id IS NOT NULL;

CREATE TABLE device_status (
    device_id         TEXT PRIMARY KEY,
    wifi_rssi_dbm     INTEGER,
    supply_voltage_v REAL,
    uptime_sec        INTEGER,
    last_seen         INTEGER NOT NULL
);
