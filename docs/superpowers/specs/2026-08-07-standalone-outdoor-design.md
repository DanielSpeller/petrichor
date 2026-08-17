# Historical Standalone Outdoor Design, Superseded

Date: 2026-08-07
Status: Superseded. V1 moved to indoor plant validation on 2026-08-14 and all future
profiles are now indoor.

This document records a discarded battery-powered outdoor proposal. It does not describe
the active V1 build or any future deployment. Use the indoor hardware and deployment
profiles in the root `SPEC.md`. This file is retained only as a historical decision record.

## Context

The remote-prep phase (see `docs/superpowers/specs/2026-07-23-remote-prep-design.md`) built
firmware and a Raspberry Pi dashboard as two independent halves meeting at `SPEC.md`. Real
hardware is now being ordered. Decision: don't deploy the Pi yet. Ship a standalone ESP32
unit that waters correctly and reports status to the cloud on its own, outdoors, on battery.
The Pi/dashboard remains a drop-in addition for later — nothing here should require
rewriting it when it arrives.

This design covers what changes in `firmware/` for standalone operation, and a new component:
a Cloudflare Worker + D1 service that receives status pushes and serves a public history page.

## Goals

- ESP32 runs the full watering decision loop (unchanged: threshold, hysteresis, cooldown,
  schedule — see remote-prep design) with no dependency on WiFi, a broker, or a Pi being
  present.
- Moisture is checked and watered on frequently enough to be accurate; the network radio,
  which is the dominant power cost, is used far less often than the check loop.
- Status/history is visible from anywhere via a public Cloudflare-hosted page, without a
  home broker or Pi running.
- The existing MQTT contract (`SPEC.md` §1) and the Pi dashboard (`dashboard/`, `data/`)
  are untouched and still valid — MQTT publishing stays in the firmware, dormant, so the Pi
  is a true drop-in later.
- Runs on battery outdoors, in a weatherproof enclosure, for roughly 1-2 weeks between
  recharges.

## Out of scope for this phase

- The Raspberry Pi dashboard itself. `dashboard/` and `data/` are left exactly as they are —
  not deployed, not modified, not deleted.
- Solar charging. Recharge is manual (swap battery or plug in via USB).
- A local web UI on the ESP32. Superseded by the cloud status page.
- Auth on the Cloudflare status page. Public, no login, per explicit decision.
- Multi-device support beyond what `SPEC.md` already carries (`device_id` field, unused
  beyond one device).

## Firmware changes (`firmware/`)

### Two decoupled loops

The existing `watering_controller`/`schedule` decision logic (pure C++, host-tested, from the
remote-prep phase) does not change. What changes is when it runs and what wraps it:

- **Check/water loop — every ~5-15 minutes.** Wake from deep sleep, read moisture
  (`readMoisturePercent()`), run the existing decision logic, drive the pump relay if needed.
  No WiFi radio involved. This is the loop that has to be frequent and accurate — a plant
  shouldn't dry out because the device was only checking every few hours.
- **Cloud sync loop — every ~4-6 hours.** On this wake, additionally bring WiFi up: resync
  NTP time, push accumulated status to the Cloudflare Worker (HTTP POST), and attempt an
  MQTT publish (silently no-ops if no broker is reachable — this is the dormant Pi hook).

A counter in RTC memory (survives deep sleep, resets on power loss) tracks how many
check/water cycles have happened since the last cloud sync, so the device knows when the
longer loop is due without needing the radio to check the time.

### `sleep_manager.cpp` gets wired in

This module exists from the remote-prep phase but was never called from `main.cpp` and never
tested on real silicon. This is its first real usage: `main.cpp`'s loop becomes
check-water-decide-sleep, with the sleep duration and wake reason (timer) driving the two-tier
logic above. Needs verification on real hardware once it arrives — deep sleep current draw
and wake timing accuracy aren't verifiable on the `native` test target.

### Time keeping across sleep

NTP resync only happens during the ~4-6 hourly cloud-sync wakes (needs WiFi). Between syncs,
`currentUnixTimeSec()` relies on the ESP32's RTC, which keeps counting through deep sleep as
long as power is maintained. Expected drift over a 4-6 hour gap is small (typical ESP32 RTC
drift is on the order of seconds per hour), acceptable for an hour-granularity schedule
window (`SCHEDULE_WINDOW_START_HOUR`/`END_HOUR`) and for watering-event timestamps. Flagged
as a real-hardware verification item, not a blocking design concern.

### Battery voltage becomes real

`readBatteryVoltageV()` (currently mocked, returns `3.98f`) becomes an actual ADC read of the
18650 cell through a voltage divider, scaled for the ESP32 ADC's input range. This is
reported in the existing `garden/device/status` MQTT payload and in the Cloudflare status
push (new field, see below).

### What does NOT change

- `watering_controller.cpp`/`.h`, `schedule.cpp`/`.h` — decision logic, untouched.
- `mqtt_client.cpp`/`.h`, `request_id_history.cpp`/`.h` — stay in the firmware, dormant.
  `SPEC.md` §1 remains the contract for whenever the Pi is added.
- `config_store.cpp`/`.h`, `ota_handler.cpp`/`.h`, `pump_runner.cpp`/`.h`, `watchdog.cpp`/`.h`
  — no changes required by this design.
- `dashboard/`, `data/` — untouched.

## Cloud component (new): Cloudflare Worker + D1

### Why D1 over KV

A single-latest-value store (KV) would only ever show current status, not trends. D1
(Cloudflare's SQLite-compatible database) can hold a real time series. Reusing the exact
schema already defined in `SPEC.md` §2 (`readings`, `watering_events`, `device_status`) means
the Worker's data shape is identical to what the Pi dashboard already expects — if the Pi is
added later, it's reading/writing the same shape, not a different one.

### Endpoint

One authenticated write path, one public read path:

- `POST /ingest` — device pushes a batch on each cloud-sync wake: accumulated moisture
  readings since the last sync, any watering events, and current device status (RSSI,
  battery voltage, uptime). Authenticated by a shared secret (`Authorization` header or
  similar), configured in `firmware/include/secrets.h` (already gitignored, already the
  pattern used for WiFi/MQTT/OTA credentials) — this prevents arbitrary internet traffic from
  writing fake data into D1, even though the *read* side is public.
- `GET /` — public status page. No auth, per explicit decision. Renders latest moisture
  reading, pump/battery status, last-watered time, and a simple moisture-over-time chart
  pulled from `readings`, similar in spirit to the Pi dashboard's chart but serverless.

### Structure

```
cloud/
  wrangler.jsonc           # Worker + D1 binding config
  schema.sql                # SPEC.md §2 schema, applied to D1
  src/
    index.ts                # POST /ingest (auth-checked, batch insert), GET / (status page)
```

### Data flow

Device (every 4-6h, WiFi up) → `POST /ingest` (batched readings + events + status) → Worker
validates shared secret → inserts into D1 (`readings`, `watering_events`, `device_status`
upsert) → next visit to `GET /` renders current data straight from D1.

## Hardware (BOM — decided in this design)

| Part | Decision |
|---|---|
| Power | 18650 Li-ion cell + TP4056 USB charge/protection module |
| Enclosure | Off-the-shelf IP65+ project box, cable glands for sensor cable / pump wires / antenna |
| Remote visibility | Cloudflare Worker + D1 (no Pi, no local web server) |

Open at ordering time (don't affect this design): moisture sensor exact model (capacitive,
not resistive), pump voltage/type, relay vs. logic-level MOSFET for the pump drive circuit.

## Testing

- `watering_controller`/`schedule` — existing `pio test -e native` suite, unchanged, still the
  source of truth for decision-logic correctness.
- New: a host-testable unit for the check/water vs. cloud-sync cycle counter logic (RTC-memory
  counter increment/reset), following the same pure-logic-no-hardware-dependency pattern.
- Worker: standard Cloudflare Workers testing (`vitest` + `wrangler`) for `/ingest` auth
  rejection and D1 insert correctness; not exercisable against real device traffic until
  hardware exists, same limitation the remote-prep MQTT client has.
- Real-hardware verification items (can't be tested on `native` or in CI): deep-sleep current
  draw/battery life, RTC drift across a 4-6h gap, ADC battery-voltage divider accuracy.
