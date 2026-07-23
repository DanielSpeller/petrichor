# Remote-Prep Design — Firmware Skeleton + Dashboard

Date: 2026-07-23
Status: Approved

## Context

The autonomous watering project is in a remote-prep phase: no ESP32, no Raspberry Pi,
no sensors, no garden access. `SPEC.md` (repo root) is the authoritative contract between
the eventual firmware and dashboard — MQTT topics/payloads, SQLite schema, shared constants.
This design covers building both sides so they're fully runnable and testable on a laptop
now, with clearly marked swap-in points for real hardware later.

Firmware and dashboard are independent programs (different languages, no shared code) that
only agree via `SPEC.md`. They are designed together here because they share that one
contract, but implemented as two separable pieces of work.

## Firmware (`firmware/`, PlatformIO, Arduino framework, C++)

### Goals

- Every real-hardware dependency isolated behind one greppable, named function.
- Watering decision logic (threshold/hysteresis/cooldown/schedule) is pure C++ with no
  Arduino/ESP32 dependency, so it can be unit tested on the host (PlatformIO `native` env)
  without any device.
- MQTT topics/payloads match `SPEC.md` §1 exactly.
- Deep sleep and watchdog logic are structured now; explicitly marked where real-device
  testing is required later.

### Structure

```
firmware/
  platformio.ini                    # env:esp32dev (real target) + env:native (host tests)
  include/
    config.h                        # SPEC.md §4 constants, MQTT topic strings, device_id
  src/
    main.cpp                        # Arduino setup()/loop(), wires modules together
    hal/
      moisture_sensor.h/.cpp        # readMoisturePercent() -- MOCK; swap point for analogRead()
      pump_relay.h/.cpp             # setPumpRelay(bool) -- MOCK; swap point for GPIO relay write
    watering_controller.h/.cpp      # pure logic: threshold+hysteresis, min duration+cooldown
    schedule.h/.cpp                 # allowed watering window + max waterings/day
    wifi_manager.h/.cpp             # connection state machine, exponential backoff (mocked state)
    mqtt_client.h/.cpp              # wraps PubSubClient; publish/subscribe per SPEC.md §1
    device_status.h/.cpp            # heartbeat payload builder (rssi/battery/uptime mocked)
    sleep_manager.h/.cpp            # deep sleep wake/sleep scaffold -- needs real-device testing
    watchdog.h/.cpp                 # watchdog setup; pump defaults OFF on reset/undefined state
  test/
    test_watering_controller/       # PlatformIO Unity tests, `pio test -e native`
```

### Watering decision logic

`watering_controller` takes `(moisture_pct, now, last_watering_end_time, todays_watering_count)`
plus the `schedule` module's window check, and returns a decision (water / don't) with a
reason. It never touches hardware or the network, which is what makes it host-testable.
`schedule` can veto a moisture-triggered water (out of window, max/day reached); moisture
readings never force a watering outside the schedule window.

### MQTT

`mqtt_client` wraps `PubSubClient`, publishing/subscribing on the exact topics, QoS, and
retained flags in `SPEC.md` §1: `garden/sensor/moisture` (pub, QoS0), `garden/pump/command`
(sub, QoS1), `garden/pump/status` (pub, QoS1), `garden/device/status` (pub, QoS0, retained).
Payload structs mirror the JSON field names/types exactly.

**Testing limitation, called out explicitly:** `pio test -e native` only exercises
`watering_controller`/`schedule` (no hardware/network deps). The MQTT client needs a real
`WiFiClient`/TCP stack and only builds against the `esp32dev` target — it will not run
without an actual ESP32 connecting to a locally-running Mosquitto broker. It's structured
and stubbed correctly against `SPEC.md`, but not exercisable end-to-end until real hardware
exists. Mosquitto is documented as a dev dependency in `firmware/README.md` even though
nothing can talk to it yet.

### WiFi, sleep, watchdog

- `wifi_manager`: state machine (`DISCONNECTED -> CONNECTING -> CONNECTED`, retry with
  exponential backoff). Mocked `wifiConnect()`/`wifiIsConnected()` stand in for the real
  `WiFi.begin()`/`WiFi.status()` calls; swap point clearly marked.
- `sleep_manager`: wake/sleep cycle scaffolded per typical ESP32 deep-sleep pattern
  (`esp_sleep_enable_timer_wakeup`, etc.), commented that it requires real-device testing —
  not exercised by the native test suite.
- `watchdog`: sets up the hardware watchdog timer and ensures the pump relay's default/reset
  state is OFF, per the safety requirement. This logic is straightforward enough to unit test
  natively (the "default off" invariant, not the actual watchdog timer hardware).

### Test harness

`pio test -e native` runs Unity tests covering:
1. Dry soil (below `MOISTURE_THRESHOLD_PCT`) triggers a water decision.
2. Hysteresis: moisture hovering near the threshold doesn't flap (must reach
   threshold + hysteresis before re-arming).
3. Cooldown: a second trigger within `COOLDOWN_PERIOD_SEC` of the last watering's end is
   blocked.
4. Schedule: a moisture trigger outside the allowed window is blocked; the window doesn't
   force a water on its own.

## Dashboard (`dashboard/`, Flask, SQLite)

### Framework choice

Flask, over FastAPI: this is a low-traffic, single-user, local dashboard reading a small
SQLite file. Synchronous request handling is sufficient, and Flask's simplicity (Jinja2
server-rendered page + Chart.js, `python app.py` to run) fits better than FastAPI's
async/auto-docs machinery, which nothing here needs.

### Structure

```
dashboard/
  app.py                  # entry point; single command `python app.py`
  requirements.txt
  data_access.py          # DAL: get_readings(), get_watering_events(), get_device_status()
  templates/index.html    # moisture chart + watering event log table + device status indicator
  static/css/, static/js/ # Chart.js config, dark theme
  README.md               # run instructions (updated)

data/
  schema.sql               # SPEC.md §2 schema verbatim
  generate_fake_data.py    # builds data/garden.db fixture
```

### Data access layer

`data_access.py` only issues `SELECT`s against a configured `DB_PATH`. It has no notion of
who wrote the rows. This is what makes swapping `generate_fake_data.py` for a future
`mqtt_subscriber.py` (which writes to the same schema) a drop-in change — the dashboard
(routes, templates, DAL) doesn't change when real data starts flowing in.

### Fake data generator

`data/generate_fake_data.py` builds `data/garden.db` matching `SPEC.md` §2 exactly, with
7 days of history for `device_id = "zone_1"`:
- `readings`: moisture drifting down over hours (drying soil), jumping up shortly after each
  watering event, on a realistic multi-times-per-day cadence.
- `watering_events`: rows corresponding to the drops/jumps in `readings`, `status =
  'completed'` for most, matching `moisture_before_pct`/`moisture_after_pct` to nearby
  readings, trigger types mixed between `moisture` and `schedule`.
- `device_status`: one upserted row with plausible `wifi_rssi_dbm`, `battery_voltage_v`,
  `uptime_sec`, recent `last_seen`.

### Frontend

Dark theme. Chart.js line chart of moisture % over time; a table of watering events (time,
trigger, result, duration, moisture before/after); a status indicator showing online/offline
(derived from `last_seen` recency), last seen time, RSSI, and battery voltage.

### Run story

One-time: `pip install -r dashboard/requirements.txt`, `python data/generate_fake_data.py`.
Then: `python dashboard/app.py` as the single command to launch the dashboard. Documented in
`dashboard/README.md`.

## Out of scope for this phase

- Real MQTT subscriber writing to SQLite (the DAL is structured so this drops in later; not
  built now).
- A Python "fake ESP32" MQTT simulator publishing/subscribing against Mosquitto to test the
  full contract end-to-end — not requested; noted here as a possible future addition if
  wanted, not built now.
- Any multi-device routing (per `SPEC.md` scope note).
- TLS/auth on MQTT (undefined in `SPEC.md`).
