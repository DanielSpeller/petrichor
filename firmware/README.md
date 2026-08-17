# firmware

ESP32 firmware: moisture sensor + MOSFET-controlled pump, talking MQTT to the Pi.

See `/SPEC.md` before changing any MQTT payload or DB field.

Copy the cloud values from `include/secrets.h.example` into the gitignored
`include/cloud_secrets.h`. WiFi and MQTT credentials remain in `include/secrets.h`.

## Indoor V1 cycle

Every real-hardware call is isolated behind one named function, each marked
`HARDWARE SWAP POINT` in its header comment:

| Function | File | Real implementation (when hardware exists) |
|---|---|---|
| `readMoisturePercent()` | `src/hal/moisture_sensor.cpp` | `analogRead()` + conversion to % |
| `setPumpRelay(bool)` | `src/hal/pump_relay.cpp` | `digitalWrite(PUMP_DRIVER_PIN, ...)` to the MOSFET driver |
| `wifiConnect()` / `wifiIsConnected()` | `src/wifi_manager.cpp` | `WiFi.begin()` / `WiFi.status()` |
| `readWifiRssiDbm()` | `src/device_status.cpp` | `WiFi.RSSI()` |
| `readSupplyVoltageV()` | `src/device_status.cpp` | optional low-voltage supply ADC read |
| `currentUnixTimeSec()` | `src/hal/clock.cpp` | NTP-synced time via `configTime()` |
| `currentLocalHour()` | `src/hal/clock.cpp` | POSIX TZ string via `setenv`/`tzset` |
| Runtime config | `src/config_store.cpp` | ESP32 `Preferences` |
| OTA updates | `src/ota_handler.cpp` | `ArduinoOTA` |
| `sendIngestPayload()` | `src/cloud_client.cpp` | HTTPS POST to the deployed Worker |

`setup()` performs one moisture check, waters if needed, and synchronises when due. V1 uses a
certified low-voltage adapter and does not require deep sleep. The local watering decision
remains safe when MQTT, cloud sync, or the dashboard is unavailable. Deep sleep is optional
power optimisation for a later indoor profile, not an outdoor or battery requirement.

V1 hardware sits above a plant tray and reservoir. Keep the pump, tubing, and sensor-side
junction contained so a disconnected tube cannot reach the electronics. See `SPEC.md` §0
for the indoor liquid-safety requirements and the later indoor hydroponic and multi-plant
profiles.

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
