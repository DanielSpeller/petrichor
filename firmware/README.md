# firmware

ESP32 firmware: moisture sensor + relay-controlled pump, talking MQTT to the Pi.

See `/SPEC.md` before changing any MQTT payload or DB field.

Copy the cloud values from `include/secrets.h.example` into the gitignored
`include/cloud_secrets.h`. WiFi and MQTT credentials remain in `include/secrets.h`.

## Standalone deep-sleep cycle

Every real-hardware call is isolated behind one named function, each marked
`HARDWARE SWAP POINT` in its header comment:

| Function | File | Real implementation (when hardware exists) |
|---|---|---|
| `readMoisturePercent()` | `src/hal/moisture_sensor.cpp` | `analogRead()` + conversion to % |
| `setPumpRelay(bool)` | `src/hal/pump_relay.cpp` | `digitalWrite(PUMP_RELAY_PIN, ...)` |
| `wifiConnect()` / `wifiIsConnected()` | `src/wifi_manager.cpp` | `WiFi.begin()` / `WiFi.status()` |
| `readWifiRssiDbm()` | `src/device_status.cpp` | `WiFi.RSSI()` |
| `readBatteryVoltageV()` | `src/device_status.cpp` | real ADC read + voltage divider math |
| `currentUnixTimeSec()` | `src/hal/clock.cpp` | NTP-synced time via `configTime()` |
| `currentLocalHour()` | `src/hal/clock.cpp` | POSIX TZ string via `setenv`/`tzset` |
| Runtime config | `src/config_store.cpp` | ESP32 `Preferences` |
| OTA updates | `src/ota_handler.cpp` | `ArduinoOTA` |
| `sendIngestPayload()` | `src/cloud_client.cpp` | HTTPS POST to the deployed Worker |

`setup()` performs one moisture check, waters if needed, runs a cloud sync when due, and enters deep sleep. RTC memory retains buffered readings, watering events, cooldown state, daily counts, and the sync counter. Cloud and MQTT sync runs every `CHECKS_PER_SYNC` wakes. Deep sleep current, wake timing, RTC drift, and GPIO state retention still need real hardware verification.

## Build and test

Install PlatformIO Core once: `pip install platformio`

- Compile the real-device target (no board needed, compile-only check):
  `cd firmware && python -m platformio run -e esp32dev`
- Run the host-side unit tests (no hardware, no broker needed):
  `cd firmware && python -m platformio test -e native`

## MQTT (dev dependency, not yet exercisable end-to-end)

The firmware's MQTT client (`src/mqtt_client.cpp`) is structured and stubbed against the
exact topics/payloads in `SPEC.md` §1, but wiring in real WiFi + a real ESP32 is required to
actually exercise it. A local Mosquitto broker is the intended dev target once hardware
exists:

- macOS: `brew install mosquitto`
- Debian/Ubuntu/Raspberry Pi OS: `sudo apt install mosquitto`
- Windows: `winget install EclipseMosquitto.Mosquitto` (or download from mosquitto.org)

Run it locally with `mosquitto -v`, and update the broker address in
`firmware/include/secrets.h` (`MQTT_BROKER_HOST`) to match your machine's IP. For TLS, set
`MQTT_USE_TLS = true` and provide the broker CA certificate (or `setInsecure()` for local
testing only).

Known limitation: PubSubClient (the MQTT library used here) always publishes at QoS 0 on the
wire. `SPEC.md` specifies QoS 1 for `garden/pump/command` and `garden/pump/status`; the gap
is closed at the application layer with `garden/pump/ack` and command `request_id`
deduplication. Switching MQTT libraries is still the path to true wire-level QoS 1.
