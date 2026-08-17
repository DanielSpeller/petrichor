"""MQTT subscriber that writes incoming firmware messages to garden.db.

Subscribes to:
- garden/sensor/moisture   -> readings
- garden/device/status     -> device_status (upsert)
- garden/pump/command      -> watering_events (pending)
- garden/pump/status       -> watering_events (update completion)

Run directly: `python dashboard/subscriber.py`
"""

import json
import os
import sqlite3
import time
from pathlib import Path

import paho.mqtt.client as mqtt

REPO_ROOT = Path(__file__).resolve().parent.parent
DB_PATH = Path(os.environ.get("GARDEN_DB_PATH", str(REPO_ROOT / "data" / "garden.db")))
BROKER = os.environ.get("MQTT_BROKER", "127.0.0.1")
PORT = int(os.environ.get("MQTT_PORT", "1883"))


def get_connection():
    conn = sqlite3.connect(DB_PATH)
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def insert_reading(conn, payload):
    conn.execute(
        "INSERT INTO readings (device_id, moisture_pct, timestamp, received_at) "
        "VALUES (?, ?, ?, ?)",
        (
            payload.get("device_id"),
            payload.get("moisture_pct"),
            payload.get("timestamp"),
            int(time.time()),
        ),
    )


def upsert_device_status(conn, payload):
    conn.execute(
        "INSERT INTO device_status (device_id, wifi_rssi_dbm, supply_voltage_v, "
        "uptime_sec, last_seen) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(device_id) DO UPDATE SET "
        "wifi_rssi_dbm=excluded.wifi_rssi_dbm, "
        "supply_voltage_v=excluded.supply_voltage_v, "
        "uptime_sec=excluded.uptime_sec, "
        "last_seen=excluded.last_seen",
        (
            payload.get("device_id"),
            payload.get("wifi_rssi_dbm"),
            payload.get("supply_voltage_v"),
            payload.get("uptime_sec"),
            payload.get("timestamp"),
        ),
    )


def _latest_reading_moisture(conn, device_id):
    row = conn.execute(
        "SELECT moisture_pct FROM readings WHERE device_id = ? "
        "ORDER BY timestamp DESC LIMIT 1",
        (device_id,),
    ).fetchone()
    return row[0] if row else None


def insert_pending_watering(conn, payload):
    request_id = payload.get("request_id")
    duration = payload.get("duration_sec", 0)
    device_id = payload.get("device_id")
    moisture_before = _latest_reading_moisture(conn, device_id)

    conn.execute(
        "INSERT OR IGNORE INTO watering_events (device_id, request_id, trigger_type, status, "
        "requested_duration_sec, actual_duration_sec, moisture_before_pct, "
        "moisture_after_pct, started_at, completed_at) "
        "VALUES (?, ?, ?, 'pending', ?, NULL, ?, NULL, ?, NULL)",
        (
            device_id,
            request_id,
            payload.get("trigger"),
            duration,
            moisture_before,
            payload.get("timestamp"),
        ),
    )


def update_watering_status(conn, payload):
    request_id = payload.get("request_id")
    row = conn.execute(
        "SELECT id FROM watering_events WHERE request_id = ?",
        (request_id,),
    ).fetchone()

    if row:
        conn.execute(
            "UPDATE watering_events SET status = ?, actual_duration_sec = ?, "
            "moisture_before_pct = COALESCE(moisture_before_pct, ?), "
            "completed_at = ? WHERE request_id = ?",
            (
                payload.get("result"),
                payload.get("actual_duration_sec"),
                payload.get("moisture_before_pct"),
                payload.get("timestamp"),
                request_id,
            ),
        )
    else:
        # Local trigger or a command we missed: write a completed row directly.
        conn.execute(
            "INSERT INTO watering_events (device_id, request_id, trigger_type, status, "
            "requested_duration_sec, actual_duration_sec, moisture_before_pct, "
            "moisture_after_pct, started_at, completed_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, NULL, ?, ?)",
            (
                payload.get("device_id"),
                request_id,
                payload.get("trigger"),
                payload.get("result"),
                payload.get("requested_duration_sec"),
                payload.get("actual_duration_sec"),
                payload.get("moisture_before_pct"),
                payload.get("timestamp"),
                payload.get("timestamp"),
            ),
        )


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        print(f"Bad payload on {msg.topic}: {msg.payload!r}")
        return

    conn = get_connection()
    try:
        if msg.topic == "garden/sensor/moisture":
            insert_reading(conn, payload)
        elif msg.topic == "garden/device/status":
            upsert_device_status(conn, payload)
        elif msg.topic == "garden/pump/command":
            insert_pending_watering(conn, payload)
        elif msg.topic == "garden/pump/status":
            update_watering_status(conn, payload)
        conn.commit()
    finally:
        conn.close()


def main():
    client = mqtt.Client()
    client.on_message = on_message
    client.connect(BROKER, PORT, 60)
    client.subscribe([
        ("garden/sensor/moisture", 0),
        ("garden/device/status", 0),
        ("garden/pump/command", 1),
        ("garden/pump/status", 1),
    ])
    print(f"Connected to {BROKER}:{PORT}, writing to {DB_PATH}")
    client.loop_forever()


if __name__ == "__main__":
    main()
