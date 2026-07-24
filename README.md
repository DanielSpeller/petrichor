# petrichor

> *petrichor* — the smell of rain on dry soil.

Autonomous garden watering: an ESP32 reads soil moisture and drives a relay-controlled
pump, reporting over MQTT to a Raspberry Pi that logs to SQLite and serves a web dashboard.

**Status: remote-prep.** No hardware exists yet. Both halves are built and tested against
mocks — the firmware behind a hardware abstraction layer, the dashboard against a
fake-data generator. When the ESP32 and Pi arrive, the mocks get swapped for real I/O and
the two sides should meet in the middle. [`SPEC.md`](SPEC.md) is what makes that possible.

## Architecture

```
   ESP32                                    Raspberry Pi
 ┌────────────────────────┐               ┌───────────────────────────┐
 │ moisture sensor  (ADC) │               │ MQTT broker (Mosquitto)   │
 │ pump relay      (GPIO) │  ── MQTT ──▶  │ SQLite  (data/garden.db)  │
 │ watering controller    │  ◀── MQTT ──  │ Flask dashboard  :5000    │
 └────────────────────────┘               └───────────────────────────┘
```

Four topics carry everything: `garden/sensor/moisture`, `garden/pump/command`,
`garden/pump/status`, `garden/device/status`. Three tables store it: `readings`,
`watering_events`, `device_status`.

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
untuned against real soil.

## Layout

| Path | What's in it |
|---|---|
| `SPEC.md` | The firmware ⇄ dashboard contract. Read before changing anything that crosses the wire. |
| `firmware/` | PlatformIO / Arduino ESP32 firmware. Hardware calls isolated behind a HAL; decision logic is pure C++ and unit-tested on the host. |
| `dashboard/` | Flask app, read-only SQLite data-access layer, Chart.js frontend. |
| `data/` | SQLite schema and the fake-data generator used in place of live MQTT. |
| `docs/` | Design specs and implementation plans. |

## Quickstart

Firmware — no board required for either command:

```bash
pip install platformio
cd firmware
python -m platformio test -e native     # host unit tests (16 cases)
python -m platformio run -e esp32dev    # compile-only check for the real target
```

Dashboard:

```bash
python -m pip install -r dashboard/requirements.txt
python data/generate_fake_data.py       # builds data/garden.db — 7 days of fake history
python dashboard/app.py                 # http://127.0.0.1:5000
cd dashboard && python -m pytest        # 9 cases
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

Known gaps, both deliberate for this phase:

- PubSubClient publishes at QoS 0 regardless, while `SPEC.md` asks for QoS 1 on the pump
  command/status topics. Needs a different library or app-level acks.
- A watering run blocks the main loop for its duration, so an incoming `stop` command can't
  interrupt it. Becomes a non-blocking state machine once "stop early" has to actually work.
