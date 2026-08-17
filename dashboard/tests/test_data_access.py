import sqlite3
from pathlib import Path

import pytest

from data_access import get_readings, get_watering_events, get_device_status

REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = REPO_ROOT / "data" / "schema.sql"


@pytest.fixture
def db_path(tmp_path):
    path = tmp_path / "test.db"
    conn = sqlite3.connect(path)
    conn.executescript(SCHEMA_PATH.read_text())
    conn.execute(
        "INSERT INTO readings (device_id, moisture_pct, timestamp, received_at) VALUES (?, ?, ?, ?)",
        ("zone_1", 42.5, 1000, 1001),
    )
    conn.execute(
        "INSERT INTO watering_events (device_id, request_id, trigger_type, status, "
        "requested_duration_sec, actual_duration_sec, moisture_before_pct, moisture_after_pct, "
        "started_at, completed_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        ("zone_1", "req-1", "moisture", "completed", 15, 15, 28.0, 60.0, 1000, 1015),
    )
    conn.execute(
        "INSERT INTO device_status (device_id, wifi_rssi_dbm, supply_voltage_v, uptime_sec, last_seen) "
        "VALUES (?, ?, ?, ?, ?)",
        ("zone_1", -60, 3.98, 86400, 2000),
    )
    conn.commit()
    conn.close()
    return str(path)


def test_get_readings_returns_matching_device_rows(db_path):
    readings = get_readings(db_path, "zone_1")
    assert len(readings) == 1
    assert readings[0]["moisture_pct"] == 42.5
    assert readings[0]["timestamp"] == 1000


def test_get_readings_filters_by_since(db_path):
    assert get_readings(db_path, "zone_1", since_unix=1001) == []
    assert len(get_readings(db_path, "zone_1", since_unix=1000)) == 1


def test_get_watering_events_returns_matching_device_rows(db_path):
    events = get_watering_events(db_path, "zone_1")
    assert len(events) == 1
    assert events[0]["request_id"] == "req-1"
    assert events[0]["status"] == "completed"


def test_get_device_status_returns_row(db_path):
    status = get_device_status(db_path, "zone_1")
    assert status["wifi_rssi_dbm"] == -60


def test_get_device_status_returns_none_for_unknown_device(db_path):
    assert get_device_status(db_path, "zone_9") is None
