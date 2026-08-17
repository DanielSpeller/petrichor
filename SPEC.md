# SPEC.md — Firmware ⇄ Dashboard Contract

This is the single source of truth for everything that crosses the boundary between the
**ESP32 firmware** (C++) and the **Raspberry Pi dashboard** (Python): MQTT topics, JSON
payload shapes, and the SQLite schema. Nothing else enforces agreement between these two
programs — no shared types, no compiler, no test that spans both languages. If you change
a field name, a unit, or a table column, **update this file in the same change**, and treat
a mismatch between this file and either codebase as a bug.

Current phase: indoor V1 hardware bring-up. The firmware still has host-testable mocks, but
the first physical deployment is a single potted plant indoors. The MQTT broker, dashboard,
and cloud sync remain optional during local validation. Every future Petrichor version is an
indoor module. Soil, hydroponic, and multi-plant versions are separate indoor profiles.

Scope: this spec assumes a **single device** (one ESP32, one moisture sensor, one pump).
`device_id` is still present everywhere so the schema and topic shapes don't need to change
if a second device is added later — but no multi-device routing (per-device topic paths,
fan-out logic, etc.) exists yet. Don't build for it until it's actually needed.

---

## 0. Concrete V1 hardware design

This section fixes the first indoor build. Substitute parts only after recording the
electrical change here and checking the firmware pin map.

### Bill of materials

| Part | Required choice | Fixed requirement |
|---|---|---|
| Controller | ESP32 DevKitC-class board using an ESP32-WROOM-32 module | PlatformIO target remains `esp32dev`; 3.3 V GPIO; Wi-Fi enabled during sync |
| Moisture sensor | [DFRobot Gravity SEN0193](https://wiki.dfrobot.com/sen0193/) capacitive analogue sensor | Supply 3.3 V; output 0–3.0 V; connect output to GPIO33; calibrate in the target soil |
| Pump | [Adafruit Peristaltic Liquid Pump, product 3910](https://www.adafruit.com/product/3910) | 5–6 V DC; nominal 5 V; 500 mA; up to 100 mL/min; use the supplied tubing for bench tests |
| Pump driver | [Adafruit MOSFET Driver](https://learn.adafruit.com/adafruit-mosfet-driver) | Low-side switching; GPIO27 signal; 3–30 V load rail; integrated AO3406 MOSFET and 1N4007 flyback diode |
| Power supply | Certified regulated 5 V DC adapter | Use a current-rated adapter with margin above the pump's measured startup demand; keep mains wiring enclosed and outside the project wiring |
| Backup battery | Optional future indoor feature | Not part of indoor V1; any later battery must remain inside the indoor power and containment design |
| Charger | Only if a future indoor backup battery is approved | Not part of indoor V1 |
| 5 V regulator | Adapter rail for V1 | The certified 5 V adapter feeds the pump rail and ESP32 VIN/5V input for V1 |
| Supply measurement | Optional future GPIO32 circuit | Leave GPIO32 unconnected in V1 unless the low-voltage supply rail is measured |
| Protection | Low-voltage inline fuse or protected adapter output | Protect the pump branch at a rating chosen from measured startup current |
| Enclosure | Indoor splash-resistant project box with a gasketed lid | Mount above the plant tray; keep it out of the tray's spill zone; use strain relief and sealed cable entries where liquid could reach the electronics |
| Containment | Plant saucer or waterproof tray plus reservoir | A failed tube or pump must drain into containment rather than onto furniture or electronics |

The Adafruit pump draws 500 mA according to its published specification. Measure startup
current with the selected adapter and fuse before unattended operation. If the ESP32 resets
when the pump starts, stop the build and replace the power arrangement rather than increasing
the fuse rating.

### Power rails and wiring rules

| Rail | Source | Loads | Limit |
|---|---|---|---|
| `5V` | Certified regulated adapter | Pump, MOSFET load side, ESP32 DevKit VIN/5V input | Regulated 5.0 V; verify at pump start |
| `3V3` | ESP32 DevKit regulator | SEN0193 and MOSFET-driver logic input | Never expose 5 V to an ESP32 GPIO |

Connect the ESP32 ground, sensor ground, MOSFET-driver logic ground, and pump-supply ground
together. Route pump current from the 5 V adapter and ground directly to the MOSFET driver.
Do not route pump current through the ESP32 board or breadboard power rails.

### Pin map

| ESP32 GPIO | Function | Electrical rule |
|---:|---|---|
| 32 | Reserved supply ADC, ADC1_CH4 | Leave unconnected in indoor V1; use only for a documented low-voltage supply monitor in a later indoor profile |
| 33 | SEN0193 analogue output, ADC1_CH5 | Input only in firmware; sensor output must stay at or below 3.0 V |
| 27 | Pump MOSFET driver signal | Output; initialise LOW before any other pump control; LOW means pump off |

GPIO32 and GPIO33 use ADC1 so Wi-Fi sync cannot invalidate their readings. Do not move
either analogue input to ADC2. Do not use GPIO12 for any part of this circuit because it
is an ESP32 flash-voltage strapping pin.

### Commissioning calibration

The sensor conversion uses two measured raw ADC endpoints, stored as runtime calibration
values:

- `sensor_air_raw`: sensor held in air above the target soil
- `sensor_water_raw`: sensor fully surrounded by water during a short calibration test

Convert linearly from those endpoints to 0–100%, clamp the result, then validate the
reading against dry, normally watered, and saturated target soil. The initial watering
values in §4 remain the starting configuration until this test produces a better set.

The pump flow value is also measured rather than assumed. Run the pump for 60 seconds into
a graduated container, record millilitres delivered, and store the measured rate for
installation records. The controller still uses timed runs; flow calibration verifies
that a 10-second run delivers the intended volume.

### Build gates

Complete these gates in order:

1. ESP32 plus SEN0193 on USB power. Verify GPIO33 readings and calibration.
2. ESP32 plus MOSFET driver and pump on a current-limited 5 V bench supply. Verify GPIO27
   starts LOW, the pump stops on reset, and the flyback protection is present.
3. Add the indoor power adapter, pump fuse, plant tray, reservoir, and tubing. Measure 5 V
   rail voltage, pump start-up current, adapter temperature, and ESP32 brownout behaviour.
4. Run the assembled indoor rig for 24 hours with a catch tray and verify that every pump
   run stops at its timeout.
5. Install the sensor in the final potting mix, calibrate it, and run a supervised watering
   test before unattended operation.

### Indoor liquid-safety requirements

- Mount the electronics above the plant tray and reservoir. Keep the enclosure outside the
  tray's spill zone.
- Make a downward drip loop in every cable that could carry water toward the enclosure. Use
  correctly sized strain relief or sealed cable glands where liquid exposure is possible.
- Keep every connector, splice, fuse holder, and exposed terminal inside the enclosure.
  Do not rely on heat-shrink alone as containment.
- Use a sensor with a moulded cable, or seal the sensor-side cable junction. The sensor probe
  may be in damp soil; its electrical junction must remain dry.
- Keep the reservoir below the electronics. Secure tubing so a disconnected tube cannot
  spray or siphon onto the enclosure.
- Test the pump, tubing, tray, and reservoir with plain water before connecting the plant or
  leaving the system unattended.

### Indoor deployment profiles and future hydroponics

| Profile | Medium | Power | Liquid safety | Status |
|---|---|---|---|---|
| V1 indoor plant | Potting mix | Certified 5 V adapter | Tray, reservoir, drip loops, splash-resistant enclosure | Active |
| V2 indoor hydroponic module | Nutrient solution | Defined low-voltage indoor supply | Reservoir containment, leak detection, secured tubing, corrosion-compatible parts | Future |
| V3 indoor multi-plant or vertical module | Soil, substrate, or nutrient solution | Defined low-voltage indoor supply | Module-level containment, leak detection, secured tubing, service access | Future |

All profiles remain indoors. Hydroponics is a new sensing profile, not a change of label for
the soil sensor. A future hydroponic module must define water level, temperature, flow,
reservoir volume, and plant-support requirements before selecting additional pH, EC,
dissolved-oxygen, or light sensors. The current `moisture_pct` field describes a soil or
potting-medium reading and must not represent nutrient-solution chemistry. Add new versioned
fields and calibration records when that module is designed.

---

## 1. MQTT Topics

Broker assumed: standard MQTT 3.1.1. TLS is optional on the firmware side via
`WiFiClientSecure`; when enabled, the broker must present a certificate the device can
validate (or the device must be explicitly configured to skip validation for local testing).
All payloads are JSON, UTF-8, no trailing newline.

### `garden/sensor/moisture`

- **Direction:** ESP32 publishes.
- **Purpose:** A single moisture reading, taken and converted to a percentage on-device.
- **QoS / retained:** QoS 0, not retained (a stream of readings; a stale retained value is
  worse than a missed one).

| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `device_id` | string | — | lowercase snake_case, see [Conventions](#3-conventions) |
| `moisture_pct` | number (float) | 0–100 | Already converted from raw ADC on the ESP32. Never publish raw ADC counts. |
| `timestamp` | integer | Unix epoch seconds, UTC | When the sensor was read, not when it was published (should be ~equal, but this is the semantic) |

```json
{
  "device_id": "zone_1",
  "moisture_pct": 42.5,
  "timestamp": 1753277940
}
```

### `garden/pump/command`

- **Direction:** Pi publishes, ESP32 subscribes.
- **Purpose:** Tells the pump to run (or stop early).
- **QoS / retained:** QoS 1 (at-least-once — a dropped command means a plant doesn't get
  watered), not retained.

| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `device_id` | string | — | Target device. ESP32 ignores commands not addressed to its own `device_id`. |
| `request_id` | string | UUID v4 recommended for Pi-issued commands. | Generated by the Pi. Echoed back in `garden/pump/status` so the two messages can be correlated. |
| `command` | string enum | `"run"` \| `"stop"` | `"stop"` cancels an in-progress run early. |
| `duration_sec` | integer | seconds, > 0 | **Required when `command == "run"`.** Omitted/ignored for `"stop"`. |
| `trigger` | string enum | `"moisture"` \| `"schedule"` \| `"manual"` | Why the Pi decided to water. Passed through so firmware/dashboard logs agree on the reason. |
| `timestamp` | integer | Unix epoch seconds, UTC | When the Pi issued the command. |

```json
{
  "device_id": "zone_1",
  "request_id": "6f2a1e2e-4b3a-4c1a-9f3f-6a2b7e1d9c44",
  "command": "run",
  "duration_sec": 15,
  "trigger": "moisture",
  "timestamp": 1753277945
}
```

### `garden/pump/status`

- **Direction:** ESP32 publishes.
- **Purpose:** Confirms what the pump actually did in response to a command (or reports it
  couldn't run — e.g. safety interlock, already running).
- **QoS / retained:** QoS 1, not retained.

| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `device_id` | string | — | |
| `request_id` | string | UUID v4 when responding to a Pi command. | Copied verbatim from the triggering `garden/pump/command` message, or a device-local identifier (e.g. `local_1`) for autonomous firmware-triggered runs. |
| `trigger` | string enum | `"moisture"` \| `"schedule"` \| `"manual"` | Copied verbatim from the command. |
| `result` | string enum | `"completed"` \| `"failed"` \| `"skipped"` | `"skipped"` = firmware declined to run (e.g. cooldown active, already running). `"failed"` = it tried and something went wrong. |
| `requested_duration_sec` | integer | seconds | What was asked for. |
| `actual_duration_sec` | integer | seconds | What actually ran. Equal to requested unless stopped early or failed partway. `0` if skipped/failed before starting. |
| `moisture_before_pct` | number (float) | 0–100 | Moisture reading immediately before the pump ran. |
| `timestamp` | integer | Unix epoch seconds, UTC | When the pump finished (or when the skip/failure was determined). |

Note: there is no `moisture_after_pct` in this payload — the firmware doesn't wait around
after a run to take a second reading. The dashboard fills in the "after" moisture later by
matching the next `garden/sensor/moisture` reading against the `watering_events` row. See
[watering_events](#watering_events).

```json
{
  "device_id": "zone_1",
  "request_id": "6f2a1e2e-4b3a-4c1a-9f3f-6a2b7e1d9c44",
  "trigger": "moisture",
  "result": "completed",
  "requested_duration_sec": 15,
  "actual_duration_sec": 15,
  "moisture_before_pct": 28.0,
  "timestamp": 1753277961
}
```

### `garden/pump/ack`

- **Direction:** ESP32 publishes, Pi subscribes.
- **Purpose:** Application-level acknowledgment that a `garden/pump/command` message was
  received and parsed. Because PubSubClient cannot send true MQTT QoS 1 publishes, this
  topic provides an at-least-once contract: the Pi may retry a command, and the ESP32 uses
  `request_id` deduplication to avoid executing the same command twice.
- **QoS / retained:** QoS 0, not retained.

| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `device_id` | string | — | |
| `request_id` | string | — | Copied verbatim from the triggering command. |
| `timestamp` | integer | Unix epoch seconds, UTC | When the ack was generated. |

```json
{
  "device_id": "zone_1",
  "request_id": "6f2a1e2e-4b3a-4c1a-9f3f-6a2b7e1d9c44",
  "timestamp": 1753277946
}
```

### `garden/device/status`

- **Direction:** ESP32 publishes.
- **Purpose:** Heartbeat / health snapshot.
- **QoS / retained:** QoS 0, **retained** (so the dashboard can show last-known health
  immediately on startup/reconnect without waiting for the next heartbeat).

| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `device_id` | string | — | |
| `wifi_rssi_dbm` | integer | dBm, typically -90 to -30 | Signal strength, negative. |
| `supply_voltage_v` | number (float) or null | volts | Optional low-voltage supply telemetry. Null when V1 has no supply monitor. It must not be described as battery voltage unless the active indoor profile includes a battery. |
| `uptime_sec` | integer | seconds since boot | Resets to 0 on every reboot — dashboard can use a drop as a reboot signal. |
| `timestamp` | integer | Unix epoch seconds, UTC | When this heartbeat was generated. |

```json
{
  "device_id": "zone_1",
  "wifi_rssi_dbm": -62,
  "supply_voltage_v": null,
  "uptime_sec": 86412,
  "timestamp": 1753277970
}
```

---

## 2. SQLite Schema

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
    supply_voltage_v REAL,
    uptime_sec        INTEGER,
    last_seen         INTEGER NOT NULL            -- unix epoch seconds, UTC; timestamp of the last heartbeat received
);
```

Notes:
- `watering_events` is written twice per event: once when the Pi issues the command
  (`status = 'pending'`, `moisture_before_pct` filled from the most recent `readings` row),
  and once when `garden/pump/status` arrives (`status`, `actual_duration_sec`,
  `completed_at` filled in). `moisture_after_pct` is backfilled by a later dashboard job
  that matches the next `readings` row for that `device_id` after `completed_at`.
- `device_status` holds one row per device — upsert on `device_id`, don't append.

---

## 3. Conventions

These apply everywhere, no exceptions, so there's no ambiguity to resolve per-field later:

- **Moisture is always a percentage, 0–100.** Never raw ADC (0–4095). The ESP32 does the
  conversion before anything is published or stored — the dashboard never sees ADC counts.
- **All timestamps are Unix epoch seconds, UTC.** Not milliseconds, not local time. Both
  sides convert to local time only at display/log-read time, never in payloads or storage.
- **All durations are in seconds**, as integers, field names suffixed `_duration_sec` or
  `_sec`.
- **Device/sensor IDs are lowercase snake_case strings**, decided once and reused
  everywhere — MQTT payloads, SQLite rows, config files, logs. For now there is exactly one:
  `zone_1`. If a second device is ever added, its ID is chosen the same way (e.g. `zone_2`)
  and this file is updated first.

---

## 4. Shared Constants

These values are **not** part of any MQTT payload, but both the firmware's watering logic
and the dashboard's alerting/display logic depend on them. They live in
`firmware/include/config.h` and are documented here so the dashboard doesn't drift out of
sync with the firmware. If the dashboard needs to show "why did/didn't it water," it should
show these numbers, not invented ones.

These are the initial commissioning values. Update this table and firmware config together
after the soil and pump calibration gates in §0 pass.

| Constant | Initial value | Meaning |
|---|---|---|
| `MOISTURE_THRESHOLD_PCT` | `30` | Below this, the firmware considers soil "dry" and eligible to trigger watering. |
| `MOISTURE_HYSTERESIS_PCT` | `5` | Soil must reach `MOISTURE_THRESHOLD_PCT + MOISTURE_HYSTERESIS_PCT` (35%) before it's considered "wet enough" again — prevents rapid re-triggering right at the threshold. |
| `MIN_WATERING_DURATION_SEC` | `10` | Shortest pump run the firmware will ever perform; requests below this are clamped up. |
| `COOLDOWN_PERIOD_SEC` | `900` (15 min) | Minimum time between the end of one watering event and the start of the next for the same device, regardless of moisture — a hard safety floor against pump chatter. |

---

## 5. Cloud Ingest Endpoint

The indoor ESP32 may batch readings, watering events, and device status to the cloud without changing the MQTT contract. Cloud sync is optional and never part of the local watering safety decision.

`POST https://<worker-host>/ingest`

Authentication uses `Authorization: Bearer <CLOUD_SHARED_SECRET>`. The body is UTF-8 JSON.

| Field | Type | Notes |
|---|---|---|
| `device_id` | string | Same convention as section 3. |
| `timestamp` | integer | Unix epoch seconds UTC for the batch. |
| `readings` | array | Objects containing `moisture_pct` and `timestamp`. May be empty. |
| `watering_events` | array | Objects containing `request_id`, `trigger`, `result`, `requested_duration_sec`, `actual_duration_sec`, `moisture_before_pct`, and `timestamp`. May be empty. |
| `device_status` | object | Contains `wifi_rssi_dbm`, optional `supply_voltage_v`, and `uptime_sec`. |

Each reading becomes a `readings` row. Each event becomes a `watering_events` row with `status` copied from `result`. Device status is upserted by `device_id`. A successful request returns `200` with `{"ok":true}`. Invalid JSON or fields return `400`. Missing or invalid authentication returns `401`.

## Related

- `firmware/` — see its own note before changing any MQTT payload.
- `dashboard/` — see its own note before changing any MQTT payload or DB field.
- `data/` — fake-data generator and the SQLite file used during development; fake data is not
  an indoor soil or indoor hydroponic observation.
