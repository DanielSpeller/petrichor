"""Reads the garden SQLite database. Only issues SELECTs -- has no idea
whether the rows were written by data/generate_fake_data.py or a future
real MQTT-subscriber. That's what makes swapping the writer later a
drop-in change.
"""

import sqlite3


def get_connection(db_path):
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    return conn


def get_readings(db_path, device_id, since_unix=None, limit=1000):
    conn = get_connection(db_path)
    try:
        if since_unix is not None:
            rows = conn.execute(
                "SELECT timestamp, moisture_pct FROM readings "
                "WHERE device_id = ? AND timestamp >= ? "
                "ORDER BY timestamp ASC LIMIT ?",
                (device_id, since_unix, limit),
            ).fetchall()
        else:
            rows = conn.execute(
                "SELECT timestamp, moisture_pct FROM readings "
                "WHERE device_id = ? ORDER BY timestamp ASC LIMIT ?",
                (device_id, limit),
            ).fetchall()
        return [dict(row) for row in rows]
    finally:
        conn.close()


def get_watering_events(db_path, device_id, limit=100):
    conn = get_connection(db_path)
    try:
        rows = conn.execute(
            "SELECT id, request_id, trigger_type, status, requested_duration_sec, "
            "actual_duration_sec, moisture_before_pct, moisture_after_pct, "
            "started_at, completed_at FROM watering_events "
            "WHERE device_id = ? ORDER BY started_at DESC LIMIT ?",
            (device_id, limit),
        ).fetchall()
        return [dict(row) for row in rows]
    finally:
        conn.close()


def get_device_status(db_path, device_id):
    conn = get_connection(db_path)
    try:
        row = conn.execute(
            "SELECT device_id, wifi_rssi_dbm, supply_voltage_v, uptime_sec, last_seen "
            "FROM device_status WHERE device_id = ?",
            (device_id,),
        ).fetchone()
        return dict(row) if row else None
    finally:
        conn.close()
