CREATE TABLE readings (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id    TEXT    NOT NULL,
    moisture_pct REAL    NOT NULL CHECK (moisture_pct >= 0 AND moisture_pct <= 100),
    timestamp    INTEGER NOT NULL,           -- unix epoch seconds, UTC; when the sensor was read
    received_at  INTEGER NOT NULL            -- unix epoch seconds, UTC; when the Pi logged it
);

CREATE INDEX idx_readings_device_timestamp ON readings(device_id, timestamp);


CREATE TABLE watering_events (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id               TEXT    NOT NULL,
    request_id              TEXT,                       -- MQTT request_id; NULL only for pre-MQTT/manual DB rows, if any
    trigger_type            TEXT    NOT NULL CHECK (trigger_type IN ('moisture', 'schedule', 'manual')),
    status                  TEXT    NOT NULL DEFAULT 'pending'
                                    CHECK (status IN ('pending', 'completed', 'failed', 'skipped')),
    requested_duration_sec  INTEGER NOT NULL,
    actual_duration_sec     INTEGER,                    -- NULL until status confirms; 0 if skipped/failed before start
    moisture_before_pct     REAL    CHECK (moisture_before_pct IS NULL OR (moisture_before_pct >= 0 AND moisture_before_pct <= 100)),
    moisture_after_pct      REAL    CHECK (moisture_after_pct  IS NULL OR (moisture_after_pct  >= 0 AND moisture_after_pct  <= 100)),
    started_at              INTEGER NOT NULL,           -- unix epoch seconds, UTC; when the command was issued
    completed_at            INTEGER                     -- unix epoch seconds, UTC; when garden/pump/status arrived
);

CREATE INDEX idx_watering_events_device_started ON watering_events(device_id, started_at);
CREATE UNIQUE INDEX idx_watering_events_request_id ON watering_events(request_id) WHERE request_id IS NOT NULL;


CREATE TABLE device_status (
    device_id         TEXT    PRIMARY KEY,
    wifi_rssi_dbm     INTEGER,
    battery_voltage_v REAL,
    uptime_sec        INTEGER,
    last_seen         INTEGER NOT NULL            -- unix epoch seconds, UTC; timestamp of the last heartbeat received
);
