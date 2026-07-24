"""Builds data/garden.db: a SQLite fixture matching SPEC.md's schema,
populated with ~7 days of plausible moisture/watering history for the
remote-prep phase (no real MQTT data exists yet).

Run directly: `python data/generate_fake_data.py`. Safe to re-run --
deletes and rebuilds the database from scratch each time.
"""

import random
import sqlite3
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path

DEVICE_ID = "zone_1"
DAYS_OF_HISTORY = 7
READING_INTERVAL_SEC = 15 * 60  # 15 minutes
MOISTURE_THRESHOLD_PCT = 30.0
MIN_WATERING_DURATION_SEC = 10

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = REPO_ROOT / "data" / "schema.sql"
DB_PATH = REPO_ROOT / "data" / "garden.db"


def build_database(db_path: Path) -> None:
    if db_path.exists():
        db_path.unlink()

    conn = sqlite3.connect(db_path)
    conn.executescript(SCHEMA_PATH.read_text())

    rng = random.Random(42)  # fixed seed: reproducible fixture

    end_time = datetime.now(timezone.utc).replace(microsecond=0)
    start_time = end_time - timedelta(days=DAYS_OF_HISTORY)

    moisture = 65.0
    current = start_time
    while current <= end_time:
        timestamp = int(current.timestamp())
        noisy_moisture = max(0.0, min(100.0, moisture + rng.uniform(-0.3, 0.3)))
        received_at = timestamp + rng.randint(1, 4)

        conn.execute(
            "INSERT INTO readings (device_id, moisture_pct, timestamp, received_at) "
            "VALUES (?, ?, ?, ?)",
            (DEVICE_ID, round(noisy_moisture, 1), timestamp, received_at),
        )

        if noisy_moisture <= MOISTURE_THRESHOLD_PCT:
            trigger_type = "moisture" if rng.random() < 0.8 else "schedule"
            is_skipped = rng.random() < 0.1

            request_id = str(uuid.uuid4())
            started_at = timestamp

            if is_skipped:
                requested_duration_sec = 15
                actual_duration_sec = 0
                completed_at = started_at
                status = "skipped"
                moisture_after = None
                # Soil stays dry -- the next reading will likely trigger again.
            else:
                requested_duration_sec = rng.randint(MIN_WATERING_DURATION_SEC, 25)
                actual_duration_sec = requested_duration_sec
                completed_at = started_at + actual_duration_sec
                status = "completed"
                moisture_after = min(100.0, noisy_moisture + rng.uniform(30, 45))
                moisture = moisture_after

            conn.execute(
                "INSERT INTO watering_events (device_id, request_id, trigger_type, status, "
                "requested_duration_sec, actual_duration_sec, moisture_before_pct, "
                "moisture_after_pct, started_at, completed_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    DEVICE_ID, request_id, trigger_type, status,
                    requested_duration_sec, actual_duration_sec,
                    round(noisy_moisture, 1),
                    round(moisture_after, 1) if moisture_after is not None else None,
                    started_at, completed_at,
                ),
            )

        # Soil dries out gradually between readings.
        moisture = max(5.0, moisture - rng.uniform(0.6, 1.0))
        current += timedelta(seconds=READING_INTERVAL_SEC)

    last_seen = int(end_time.timestamp()) - rng.randint(30, 90)
    conn.execute(
        "INSERT INTO device_status (device_id, wifi_rssi_dbm, battery_voltage_v, "
        "uptime_sec, last_seen) VALUES (?, ?, ?, ?, ?)",
        (DEVICE_ID, -58, 4.01, 2 * 24 * 3600 + rng.randint(0, 3600), last_seen),
    )

    conn.commit()
    conn.close()


if __name__ == "__main__":
    build_database(DB_PATH)
    print(f"Wrote fixture database to {DB_PATH}")
