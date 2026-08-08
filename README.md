# petrichor

> *petrichor* — the smell of rain on dry soil.

Autonomous garden watering: an ESP32 reads soil moisture and drives a relay-controlled
pump, reporting over MQTT to a Raspberry Pi that logs to SQLite and serves a web dashboard.

**Status: standalone outdoor prep.** No hardware exists yet. The firmware now runs an
offline deep sleep watering cycle and buffers cloud updates. Hardware calls remain behind
swap points and host tests. [`SPEC.md`](SPEC.md) keeps MQTT, cloud, and storage aligned.

## Architecture

```
   ESP32                                    Raspberry Pi
 ┌────────────────────────┐               ┌───────────────────────────┐
 │ moisture sensor  (ADC) │               │ MQTT broker (Mosquitto)   │
 │ pump relay      (GPIO) │  ── MQTT ──▶  │ SQLite  (data/garden.db)  │
 │ watering controller    │  ◀── MQTT ──  │ Flask dashboard  :5000    │
 └────────────────────────┘               └───────────────────────────┘
```

The current deployment uses the ESP32 as a standalone battery device. It checks and waters offline on every wake, then sends buffered history to Cloudflare on a slower cadence. The Raspberry Pi and MQTT path remain available for later.

Five topics carry everything: `garden/sensor/moisture`, `garden/pump/command`,
`garden/pump/status`, `garden/pump/ack`, `garden/device/status`. Three tables store it:
`readings`, `watering_events`, `device_status`.

Nothing enforces agreement between the C++ and the Python — no shared types, no compiler,
no test spanning both. **[`SPEC.md`](SPEC.md) is the contract**: topics, JSON payload
shapes, units, and the SQLite schema. Change a field name or a unit there in the same
commit as the code, and treat any drift between spec and code as a bug.

## Watering logic

The pump is not a timer. The firmware decides locally, so a dead broker or dead WiFi never
means a dead garden — and never means a flood either:

- **Threshold** — water when moisture drops below 30%, in fixed 10-second runs.
- **Hysteresis** — after a run the controller disarms, and won't trigger again until
  moisture has recovered past 35%. A sensor hovering at the threshold can't chatter the
  relay.
- **Cooldown** — at least 15 minutes between runs; water takes time to reach the sensor.
- **Schedule window** — only between 06:00 and 20:00 local, max 4 runs per day.
- **Safe default** — the relay is driven off on boot, on watchdog reset, and on any
  unexpected state. Off is always the failure mode.

These values are placeholders in [`firmware/include/config.h`](firmware/include/config.h),
untuned against real soil. They are loaded from ESP32 Preferences at boot so they can be
changed without reflashing, with the compile-time values used as defaults.

## Layout

| Path | What's in it |
|---|---|
| `SPEC.md` | The firmware ⇄ dashboard contract. Read before changing anything that crosses the wire. |
| `firmware/` | PlatformIO / Arduino ESP32 firmware. Hardware calls isolated behind a HAL; decision logic is pure C++ and unit-tested on the host. |
| `dashboard/` | Flask app, read-only SQLite data-access layer, Chart.js frontend. |
| `data/` | SQLite schema and the fake-data generator used in place of live MQTT. |
| `cloud/` | Cloudflare Worker and D1 service for standalone ingestion and the public status page. |
| `docs/` | Design specs and implementation plans. |

## Quickstart

Firmware — no board required for either command:

```bash
pip install platformio
cd firmware
cp include/secrets.h.example include/secrets.h   # fill in WiFi and broker credentials
python -m platformio test -e native     # host unit tests (31 cases)
python -m platformio run -e esp32dev    # compile-only check for the real target
```

Dashboard:

```bash
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r dashboard/requirements.txt  # Windows
# source .venv/bin/activate && pip install -r dashboard/requirements.txt  # macOS/Linux
python data/generate_fake_data.py       # builds data/garden.db — 7 days of fake history
python dashboard/app.py                 # http://127.0.0.1:5000
cd dashboard && ../.venv/Scripts/python.exe -m pytest  # Windows, 18 cases
# cd dashboard && python -m pytest                       # macOS/Linux with venv activated
```

End-to-end (no hardware):

```bash
# 1. Start an MQTT broker, e.g. Mosquitto, on localhost:1883.
# 2. Run the Pi subscriber in one terminal.
python dashboard/subscriber.py
# 3. Run the fake ESP32 in another terminal.
python data/fake_esp32.py
# 4. Send a pump command and wait for the ack.
python dashboard/commander.py --command run --duration 15
# 5. Open http://127.0.0.1:5000 after a few minutes of readings have accumulated.
```

## When hardware arrives

Every real-hardware call sits behind one named function marked `HARDWARE SWAP POINT` in its
header comment — moisture read, pump relay, WiFi, RSSI, battery voltage, clock. See
[`firmware/README.md`](firmware/README.md) for the full table. `sleep_manager.cpp` is
written against the ESP32 Arduino core's documented deep-sleep API but is **untested on
real silicon** and not yet wired into the main loop.

On the Pi side, the dashboard only ever reads SQLite and has no idea who wrote the rows, so
replacing the fake-data generator with a real MQTT subscriber is a drop-in change — nothing
in `dashboard/` moves.

Production-hardening status:

- Application-level QoS 1 is implemented via `garden/pump/ack` and command `request_id`
  deduplication. True MQTT QoS 1 still requires switching from PubSubClient to a library
  that supports it.
- WiFi, MQTT broker, TLS, and OTA password credentials live in `firmware/include/secrets.h`,
  which is generated from `firmware/include/secrets.h.example` and gitignored.
- MQTT TLS is supported via `WiFiClientSecure`; configure it in `secrets.h`.
- Runtime configuration (thresholds, schedule, cooldown) is persisted to ESP32 Preferences.
- Timezone/DST is handled via a POSIX TZ string in `config.h`.
- OTA updates are handled by ArduinoOTA; set a password in `secrets.h`.

Known gaps:

- `sleep_manager.cpp` is written against the ESP32 Arduino core's documented deep-sleep API
  but is untested on real silicon and not yet wired into the main loop.
