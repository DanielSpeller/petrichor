import sqlite3
from pathlib import Path

import pytest

import subscriber as sub

REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = REPO_ROOT / "data" / "schema.sql"


@pytest.fixture
def db_conn(tmp_path, monkeypatch):
    db_path = tmp_path / "test.db"
    conn = sqlite3.connect(db_path)
    conn.executescript(SCHEMA_PATH.read_text())
    conn.commit()
    monkeypatch.setattr(sub, "DB_PATH", db_path)
    yield conn
    conn.close()


def test_insert_reading_stores_moisture_row(db_conn):
    sub.insert_reading(
        db_conn,
        {"device_id": "zone_1", "moisture_pct": 33.3, "timestamp": 1000},
    )
    rows = db_conn.execute("SELECT moisture_pct FROM readings").fetchall()
    assert len(rows) == 1
    assert rows[0][0] == 33.3


def test_upsert_device_status_inserts_and_updates(db_conn):
    sub.upsert_device_status(
        db_conn,
        {
            "device_id": "zone_1",
            "wifi_rssi_dbm": -70,
            "supply_voltage_v": None,
            "uptime_sec": 120,
            "timestamp": 1000,
        },
    )
    row = db_conn.execute(
        "SELECT wifi_rssi_dbm, last_seen FROM device_status WHERE device_id = ?",
        ("zone_1",),
    ).fetchone()
    assert row == (-70, 1000)

    sub.upsert_device_status(
        db_conn,
        {
            "device_id": "zone_1",
            "wifi_rssi_dbm": -55,
            "supply_voltage_v": None,
            "uptime_sec": 240,
            "timestamp": 2000,
        },
    )
    row = db_conn.execute(
        "SELECT wifi_rssi_dbm, last_seen FROM device_status WHERE device_id = ?",
        ("zone_1",),
    ).fetchone()
    assert row == (-55, 2000)


def test_insert_pending_watering_uses_latest_reading_for_moisture_before(db_conn):
    sub.insert_reading(
        db_conn,
        {"device_id": "zone_1", "moisture_pct": 22.5, "timestamp": 1000},
    )
    sub.insert_pending_watering(
        db_conn,
        {
            "device_id": "zone_1",
            "request_id": "req-123",
            "command": "run",
            "duration_sec": 15,
            "trigger": "moisture",
            "timestamp": 1001,
        },
    )
    row = db_conn.execute(
        "SELECT status, requested_duration_sec, moisture_before_pct FROM watering_events"
    ).fetchone()
    assert row == ("pending", 15, 22.5)


def test_insert_pending_watering_ignores_retried_request(db_conn):
    payload = {
        "device_id": "zone_1",
        "request_id": "req-retried",
        "command": "run",
        "duration_sec": 15,
        "trigger": "manual",
        "timestamp": 1001,
    }
    sub.insert_pending_watering(db_conn, payload)
    sub.insert_pending_watering(db_conn, payload)

    assert db_conn.execute("SELECT COUNT(*) FROM watering_events").fetchone()[0] == 1


def test_update_watering_status_fills_pending_row(db_conn):
    sub.insert_pending_watering(
        db_conn,
        {
            "device_id": "zone_1",
            "request_id": "req-456",
            "command": "run",
            "duration_sec": 20,
            "trigger": "schedule",
            "timestamp": 1000,
        },
    )
    sub.update_watering_status(
        db_conn,
        {
            "device_id": "zone_1",
            "request_id": "req-456",
            "trigger": "schedule",
            "result": "completed",
            "requested_duration_sec": 20,
            "actual_duration_sec": 20,
            "moisture_before_pct": 25.0,
            "timestamp": 1020,
        },
    )
    row = db_conn.execute(
        "SELECT status, actual_duration_sec, completed_at FROM watering_events"
    ).fetchone()
    assert row == ("completed", 20, 1020)


def test_update_watering_status_inserts_for_local_trigger(db_conn):
    sub.update_watering_status(
        db_conn,
        {
            "device_id": "zone_1",
            "request_id": "local_1",
            "trigger": "moisture",
            "result": "completed",
            "requested_duration_sec": 10,
            "actual_duration_sec": 10,
            "moisture_before_pct": 28.0,
            "timestamp": 2000,
        },
    )
    row = db_conn.execute(
        "SELECT status, requested_duration_sec FROM watering_events"
    ).fetchone()
    assert row == ("completed", 10)
