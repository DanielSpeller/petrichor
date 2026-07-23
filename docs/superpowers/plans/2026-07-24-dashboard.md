# Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Flask + SQLite dashboard that runs entirely on a laptop against a fake-data
fixture (no real MQTT data exists yet), structured so a future real MQTT-subscriber can write
to the same database without any dashboard code changing.

**Architecture:** `data/generate_fake_data.py` builds `data/garden.db` from `data/schema.sql`
(SPEC.md's schema, verbatim) with 7 days of plausible moisture/watering history.
`dashboard/data_access.py` is a data-access layer that only issues `SELECT`s against that
database — it has no idea who wrote the rows. `dashboard/app.py` (Flask) serves a single
dark-themed page with a Chart.js moisture chart, a watering-event table, and a device status
indicator, all fed by three small JSON API routes backed by the data-access layer.

**Tech Stack:** Python 3, Flask, SQLite (stdlib `sqlite3`), pytest, Chart.js (via CDN).

## Global Constraints

- SQLite schema must match `SPEC.md` §2 exactly — table/column names, types, and CHECK
  constraints copied verbatim, no additions or omissions.
- `device_id` is `"zone_1"` (SPEC.md §3) everywhere — fixture data, queries, templates.
- Moisture is always a percentage 0-100 (SPEC.md §3). All timestamps stored/queried are Unix
  epoch seconds, UTC; convert to local time only at display time in the frontend.
- `dashboard/data_access.py` must only read from SQLite via a configurable `DB_PATH` — no
  assumptions anywhere about who wrote the rows (fake generator now, MQTT subscriber later).
- Fake data: 7 days of history for `device_id = "zone_1"`, moisture drifting down over time
  and jumping up after watering events, with corresponding `watering_events` rows and one
  `device_status` row (user-selected in design review; see
  `docs/superpowers/specs/2026-07-24-remote-prep-design.md`).
- Dashboard theme: dark (user-selected in design review). Palette tokens are fixed in Task 4
  below — reuse them, don't invent new colors.
- Runnable with a single command per `dashboard/README.md`: `python dashboard/app.py` (after
  a one-time `pip install` + fixture generation step).

---

### Task 1: SQLite schema and fake-data generator

**Files:**
- Create: `data/schema.sql`
- Create: `data/generate_fake_data.py`

**Interfaces:**
- Produces: `data/garden.db` (a SQLite file) with tables `readings`, `watering_events`,
  `device_status` matching `SPEC.md` §2 — consumed by `dashboard/data_access.py` (Task 2) and
  its tests, and by `dashboard/app.py`'s tests (Task 3).
- Produces (importable): `build_database(db_path: pathlib.Path) -> None` in
  `data/generate_fake_data.py` — reusable by tests if needed later.

- [ ] **Step 1: Write `data/schema.sql`**

```sql
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
```

- [ ] **Step 2: Write `data/generate_fake_data.py`**

```python
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
```

- [ ] **Step 3: Run it and sanity-check the output**

Run: `python data/generate_fake_data.py`
Expected: prints `Wrote fixture database to .../data/garden.db` and creates the file.

Run:
```bash
python -c "
import sqlite3
conn = sqlite3.connect('data/garden.db')
print('readings:', conn.execute('SELECT COUNT(*) FROM readings').fetchone()[0])
print('watering_events:', conn.execute('SELECT COUNT(*) FROM watering_events').fetchone()[0])
print('device_status:', conn.execute('SELECT COUNT(*) FROM device_status').fetchone()[0])
"
```
Expected: `readings` around 672 (7 days * 96 fifteen-minute intervals), `watering_events`
somewhere in the low teens (roughly one every 10-18 hours), `device_status` exactly `1`.

- [ ] **Step 4: Commit**

```bash
git add data/schema.sql data/generate_fake_data.py
git commit -m "data: add SPEC.md schema and 7-day fake-data generator"
```

Note: `data/garden.db` itself is a generated artifact, not committed — add a `.gitignore`
entry for it in Task 5 alongside the dashboard's other generated files.

---

### Task 2: Data access layer

**Files:**
- Create: `dashboard/data_access.py`
- Create: `dashboard/tests/conftest.py`
- Test: `dashboard/tests/test_data_access.py`

**Interfaces:**
- Consumes: `data/schema.sql` (Task 1).
- Produces: `get_connection(db_path) -> sqlite3.Connection`,
  `get_readings(db_path, device_id, since_unix=None, limit=1000) -> list[dict]`,
  `get_watering_events(db_path, device_id, limit=100) -> list[dict]`,
  `get_device_status(db_path, device_id) -> dict | None` — consumed by `dashboard/app.py`
  (Task 3).

- [ ] **Step 1: Write `dashboard/tests/conftest.py`**

This makes `dashboard/` importable from test files regardless of where `pytest` is invoked
from, and is shared by every test file in this plan.

```python
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
```

- [ ] **Step 2: Write the failing test**

`dashboard/tests/test_data_access.py`:

```python
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
        "INSERT INTO device_status (device_id, wifi_rssi_dbm, battery_voltage_v, uptime_sec, last_seen) "
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
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd dashboard && python -m pytest tests/test_data_access.py -v`
Expected: FAIL/ERROR — `data_access` module does not exist yet.

- [ ] **Step 4: Write `dashboard/data_access.py`**

```python
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
            "SELECT device_id, wifi_rssi_dbm, battery_voltage_v, uptime_sec, last_seen "
            "FROM device_status WHERE device_id = ?",
            (device_id,),
        ).fetchone()
        return dict(row) if row else None
    finally:
        conn.close()
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd dashboard && python -m pytest tests/test_data_access.py -v`
Expected: PASS — 5 tests.

- [ ] **Step 6: Commit**

```bash
git add dashboard/data_access.py dashboard/tests/conftest.py dashboard/tests/test_data_access.py
git commit -m "dashboard: add SQLite data-access layer"
```

---

### Task 3: Flask app and JSON API routes

**Files:**
- Create: `dashboard/app.py`
- Create: `dashboard/requirements.txt`
- Test: `dashboard/tests/test_app.py`

**Interfaces:**
- Consumes: `get_readings`, `get_watering_events`, `get_device_status` from
  `dashboard/data_access.py` (Task 2).
- Produces: Flask app object `app` in `dashboard/app.py`; routes `GET /` (renders
  `templates/index.html`), `GET /api/readings`, `GET /api/watering-events`,
  `GET /api/device-status` (all JSON); module-level `DB_PATH` read from the
  `GARDEN_DB_PATH` environment variable (default `data/garden.db` relative to the repo root)
  — consumed by `templates/index.html`/`static/js/chart_setup.js` (Task 4) via `fetch()`.

- [ ] **Step 1: Write `dashboard/requirements.txt`**

```
Flask==3.0.3
pytest==8.2.0
```

- [ ] **Step 2: Write the failing test**

`dashboard/tests/test_app.py`:

```python
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
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd dashboard && pip install -r requirements.txt && python -m pytest tests/test_app.py -v`
Expected: FAIL/ERROR — `app` module does not exist yet.

- [ ] **Step 4: Write `dashboard/app.py`**

```python
import os
from pathlib import Path

from flask import Flask, jsonify, render_template

from data_access import get_readings, get_watering_events, get_device_status

REPO_ROOT = Path(__file__).resolve().parent.parent
DB_PATH = os.environ.get("GARDEN_DB_PATH", str(REPO_ROOT / "data" / "garden.db"))
DEVICE_ID = "zone_1"

app = Flask(__name__)


@app.route("/")
def index():
    return render_template("index.html", device_id=DEVICE_ID)


@app.route("/api/readings")
def api_readings():
    readings = get_readings(DB_PATH, DEVICE_ID, limit=2000)
    return jsonify(readings)


@app.route("/api/watering-events")
def api_watering_events():
    events = get_watering_events(DB_PATH, DEVICE_ID, limit=200)
    return jsonify(events)


@app.route("/api/device-status")
def api_device_status():
    status = get_device_status(DB_PATH, DEVICE_ID)
    return jsonify(status)


if __name__ == "__main__":
    app.run(debug=True, port=5000)
```

Note: `templates/index.html` doesn't exist until Task 4, so `test_index_page_loads` will
still fail after this step with a `TemplateNotFound` error — that's expected here; it will
pass once Task 4 adds the template.

- [ ] **Step 5: Run test to verify the API routes pass (template test still fails)**

Run: `cd dashboard && python -m pytest tests/test_app.py -v`
Expected: `test_api_readings_returns_seeded_row`, `test_api_device_status_returns_seeded_row`,
`test_api_watering_events_returns_empty_list_when_none` PASS. `test_index_page_loads` FAILS
with `jinja2.exceptions.TemplateNotFound: index.html` — expected until Task 4.

- [ ] **Step 6: Commit**

```bash
git add dashboard/app.py dashboard/requirements.txt dashboard/tests/test_app.py
git commit -m "dashboard: add Flask app and JSON API routes"
```

---

### Task 4: Frontend — dark-themed chart, event table, status indicator

**Files:**
- Create: `dashboard/templates/index.html`
- Create: `dashboard/static/css/style.css`
- Create: `dashboard/static/js/chart_setup.js`

**Interfaces:**
- Consumes: `/api/readings`, `/api/watering-events`, `/api/device-status` (Task 3, via
  client-side `fetch()`); `device_id` template variable from `app.py`'s `index()` route.
- Produces: the rendered dashboard page. Nothing consumes this — it's the top of the
  dependency graph. Completes `test_index_page_loads` from Task 3.

Color tokens below come from the dataviz skill's validated reference palette (dark surface
band) — reuse these values as-is, don't invent new ones: chart/page surface `#1a1a19` /
`#0d0d0d`, primary ink `#ffffff`, secondary ink `#c3c2b7`, muted ink `#898781`, gridline
`#2c2c2a`, series (moisture line) `#3987e5`, status good/warning/critical
`#0ca30c` / `#fab219` / `#d03b3b`.

- [ ] **Step 1: Write `dashboard/static/css/style.css`**

```css
:root {
  --page-plane: #0d0d0d;
  --surface-1: #1a1a19;
  --text-primary: #ffffff;
  --text-secondary: #c3c2b7;
  --text-muted: #898781;
  --gridline: #2c2c2a;
  --series-1: #3987e5;
  --status-good: #0ca30c;
  --status-warning: #fab219;
  --status-critical: #d03b3b;
  --border: rgba(255, 255, 255, 0.10);
}

* { box-sizing: border-box; }

body {
  margin: 0;
  background: var(--page-plane);
  color: var(--text-primary);
  font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
}

.page {
  max-width: 960px;
  margin: 0 auto;
  padding: 24px 16px 64px;
}

h1 {
  font-size: 1.25rem;
  font-weight: 600;
  margin: 0 0 24px;
}

.card {
  background: var(--surface-1);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 16px;
  margin-bottom: 24px;
}

.card h2 {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--text-secondary);
  margin: 0 0 12px;
  text-transform: uppercase;
  letter-spacing: 0.02em;
}

.status-row {
  display: flex;
  gap: 24px;
  flex-wrap: wrap;
  align-items: center;
}

.status-dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  margin-right: 6px;
}

.status-dot.online { background: var(--status-good); }
.status-dot.offline { background: var(--status-critical); }

.status-item {
  color: var(--text-secondary);
  font-size: 0.9rem;
}

.status-item strong {
  color: var(--text-primary);
  font-weight: 600;
}

table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.85rem;
}

th, td {
  text-align: left;
  padding: 8px 10px;
  border-bottom: 1px solid var(--gridline);
  color: var(--text-secondary);
  font-variant-numeric: tabular-nums;
}

th {
  color: var(--text-muted);
  font-weight: 600;
  text-transform: uppercase;
  font-size: 0.75rem;
  letter-spacing: 0.03em;
}

td.primary { color: var(--text-primary); }

.badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 0.75rem;
  font-weight: 600;
}

.badge.completed { background: rgba(12, 163, 12, 0.15); color: var(--status-good); }
.badge.failed { background: rgba(208, 59, 59, 0.15); color: var(--status-critical); }
.badge.skipped { background: rgba(250, 178, 25, 0.15); color: var(--status-warning); }
.badge.pending { background: rgba(137, 135, 129, 0.15); color: var(--text-muted); }

#moisture-chart-wrap {
  height: 320px;
}
```

- [ ] **Step 2: Write `dashboard/static/js/chart_setup.js`**

```javascript
async function loadMoistureChart() {
  const response = await fetch("/api/readings");
  const readings = await response.json();

  const labels = readings.map((r) => new Date(r.timestamp * 1000));
  const values = readings.map((r) => r.moisture_pct);

  const ctx = document.getElementById("moisture-chart").getContext("2d");
  new Chart(ctx, {
    type: "line",
    data: {
      labels: labels,
      datasets: [
        {
          label: "Moisture %",
          data: values,
          borderColor: "#3987e5",
          backgroundColor: "transparent",
          borderWidth: 2,
          pointRadius: 0,
          pointHoverRadius: 4,
          tension: 0,
        },
      ],
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      plugins: {
        legend: { display: false },
        tooltip: {
          backgroundColor: "#1a1a19",
          titleColor: "#ffffff",
          bodyColor: "#c3c2b7",
          borderColor: "#2c2c2a",
          borderWidth: 1,
        },
      },
      scales: {
        x: {
          type: "time",
          time: { unit: "hour" },
          grid: { color: "#2c2c2a" },
          ticks: { color: "#898781" },
        },
        y: {
          min: 0,
          max: 100,
          grid: { color: "#2c2c2a" },
          ticks: { color: "#898781" },
          title: { display: true, text: "Moisture %", color: "#c3c2b7" },
        },
      },
    },
  });
}

async function loadWateringEvents() {
  const response = await fetch("/api/watering-events");
  const events = await response.json();
  const tbody = document.getElementById("events-body");
  tbody.innerHTML = "";

  if (events.length === 0) {
    tbody.innerHTML = '<tr><td colspan="6">No watering events yet.</td></tr>';
    return;
  }

  events.forEach((event) => {
    const row = document.createElement("tr");
    const started = new Date(event.started_at * 1000).toLocaleString();
    const before = event.moisture_before_pct != null ? event.moisture_before_pct.toFixed(1) + "%" : "—";
    const after = event.moisture_after_pct != null ? event.moisture_after_pct.toFixed(1) + "%" : "—";
    const duration = event.actual_duration_sec != null ? event.actual_duration_sec + "s" : "—";

    row.innerHTML = `
      <td class="primary">${started}</td>
      <td>${event.trigger_type}</td>
      <td><span class="badge ${event.status}">${event.status}</span></td>
      <td>${duration}</td>
      <td>${before}</td>
      <td>${after}</td>
    `;
    tbody.appendChild(row);
  });
}

async function loadDeviceStatus() {
  const response = await fetch("/api/device-status");
  const status = await response.json();
  const container = document.getElementById("device-status");

  if (!status) {
    container.innerHTML = '<span class="status-item">No device status recorded yet.</span>';
    return;
  }

  const nowSec = Math.floor(Date.now() / 1000);
  const secondsSinceSeen = nowSec - status.last_seen;
  const isOnline = secondsSinceSeen < 300; // offline if no heartbeat in 5 minutes
  const lastSeen = new Date(status.last_seen * 1000).toLocaleString();

  container.innerHTML = `
    <span class="status-item">
      <span class="status-dot ${isOnline ? "online" : "offline"}"></span>
      <strong>${isOnline ? "Online" : "Offline"}</strong>
    </span>
    <span class="status-item">Last seen: <strong>${lastSeen}</strong></span>
    <span class="status-item">RSSI: <strong>${status.wifi_rssi_dbm} dBm</strong></span>
    <span class="status-item">Battery: <strong>${status.battery_voltage_v.toFixed(2)} V</strong></span>
  `;
}

loadMoistureChart();
loadWateringEvents();
loadDeviceStatus();
```

- [ ] **Step 3: Write `dashboard/templates/index.html`**

```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Garden Dashboard — {{ device_id }}</title>
  <link rel="stylesheet" href="{{ url_for('static', filename='css/style.css') }}">
</head>
<body>
  <div class="page">
    <h1>Garden Dashboard — {{ device_id }}</h1>

    <div class="card">
      <h2>Device Status</h2>
      <div class="status-row" id="device-status">
        <span class="status-item">Loading&hellip;</span>
      </div>
    </div>

    <div class="card">
      <h2>Moisture Over Time</h2>
      <div id="moisture-chart-wrap">
        <canvas id="moisture-chart"></canvas>
      </div>
    </div>

    <div class="card">
      <h2>Watering Events</h2>
      <table>
        <thead>
          <tr>
            <th>Started</th>
            <th>Trigger</th>
            <th>Status</th>
            <th>Duration</th>
            <th>Before</th>
            <th>After</th>
          </tr>
        </thead>
        <tbody id="events-body">
          <tr><td colspan="6">Loading&hellip;</td></tr>
        </tbody>
      </table>
    </div>
  </div>

  <script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
  <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-date-fns@3/dist/chartjs-adapter-date-fns.bundle.min.js"></script>
  <script src="{{ url_for('static', filename='js/chart_setup.js') }}"></script>
</body>
</html>
```

- [ ] **Step 4: Run the full dashboard test suite to verify everything passes**

Run: `cd dashboard && python -m pytest -v`
Expected: PASS — all 9 tests across `test_data_access.py` and `test_app.py`, including
`test_index_page_loads` (now that the template exists).

- [ ] **Step 5: Commit**

```bash
git add dashboard/templates dashboard/static
git commit -m "dashboard: add dark-themed frontend (chart, event log, status indicator)"
```

---

### Task 5: README, .gitignore, and end-to-end smoke test

**Files:**
- Modify: `dashboard/README.md`
- Create: `.gitignore`

**Interfaces:**
- Consumes: everything from Tasks 1-4.
- Produces: nothing new consumed elsewhere — this is the final integration/documentation
  task.

- [ ] **Step 1: Write `.gitignore`**

```
__pycache__/
*.pyc
.pytest_cache/
data/garden.db
.pio/
.vscode/
```

- [ ] **Step 2: Update `dashboard/README.md`**

```markdown
# dashboard

Raspberry Pi side: MQTT broker client, SQLite logging, web dashboard.

See `/SPEC.md` before changing any MQTT payload or DB field.

## Remote-prep phase (no real MQTT data yet)

This phase runs against a fake-data fixture instead of live MQTT data -- see
`data/generate_fake_data.py`. The dashboard's data-access layer (`dashboard/data_access.py`)
only reads from SQLite and has no idea who wrote the rows, so swapping the fixture for a real
MQTT-subscriber later is a drop-in change; nothing in `dashboard/` needs to change when real
data starts flowing in.

## Setup

```bash
pip install -r dashboard/requirements.txt
python data/generate_fake_data.py   # one-time: builds data/garden.db
```

## Run

```bash
python dashboard/app.py
```

Then open http://127.0.0.1:5000 in a browser.

## Tests

```bash
cd dashboard
pytest
```

## Regenerating the fixture

Re-run `python data/generate_fake_data.py` any time -- it deletes and rebuilds
`data/garden.db` from scratch with a fresh 7-day history (fixed random seed, so the shape of
the data is reproducible run-to-run).
```

- [ ] **Step 3: End-to-end smoke test — generate fixture, start the app, hit every route**

Run:
```bash
pip install -r dashboard/requirements.txt
python data/generate_fake_data.py
python dashboard/app.py &
sleep 2
curl -s -o /dev/null -w "index: %{http_code}\n" http://127.0.0.1:5000/
curl -s -o /dev/null -w "readings: %{http_code}\n" http://127.0.0.1:5000/api/readings
curl -s -o /dev/null -w "watering-events: %{http_code}\n" http://127.0.0.1:5000/api/watering-events
curl -s -o /dev/null -w "device-status: %{http_code}\n" http://127.0.0.1:5000/api/device-status
kill %1
```
Expected: all four routes report `200`.

- [ ] **Step 4: Commit**

```bash
git add .gitignore dashboard/README.md
git commit -m "dashboard: document run instructions and add .gitignore"
```
