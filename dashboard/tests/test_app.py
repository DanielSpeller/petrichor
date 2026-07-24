import sqlite3
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = REPO_ROOT / "data" / "schema.sql"


@pytest.fixture
def client(tmp_path, monkeypatch):
    db_path = tmp_path / "test.db"
    conn = sqlite3.connect(db_path)
    conn.executescript(SCHEMA_PATH.read_text())
    conn.execute(
        "INSERT INTO readings (device_id, moisture_pct, timestamp, received_at) VALUES (?, ?, ?, ?)",
        ("zone_1", 42.5, 1000, 1001),
    )
    conn.execute(
        "INSERT INTO device_status (device_id, wifi_rssi_dbm, battery_voltage_v, uptime_sec, last_seen) "
        "VALUES (?, ?, ?, ?, ?)",
        ("zone_1", -60, 3.98, 86400, 1000),
    )
    conn.commit()
    conn.close()

    monkeypatch.setenv("GARDEN_DB_PATH", str(db_path))
    import importlib
    import app as app_module
    importlib.reload(app_module)  # re-read GARDEN_DB_PATH now that it's set
    app_module.app.testing = True
    return app_module.app.test_client()


def test_index_page_loads(client):
    response = client.get("/")
    assert response.status_code == 200
    assert b"moisture-chart" in response.data


def test_api_readings_returns_seeded_row(client):
    response = client.get("/api/readings")
    assert response.status_code == 200
    data = response.get_json()
    assert len(data) == 1
    assert data[0]["moisture_pct"] == 42.5


def test_api_device_status_returns_seeded_row(client):
    response = client.get("/api/device-status")
    assert response.status_code == 200
    data = response.get_json()
    assert data["wifi_rssi_dbm"] == -60


def test_api_watering_events_returns_empty_list_when_none(client):
    response = client.get("/api/watering-events")
    assert response.status_code == 200
    assert response.get_json() == []
