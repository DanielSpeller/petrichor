#pragma once

#include <cstdint>

// SPEC.md §4 Shared Constants -- placeholder values, not yet tuned against
// real soil/hardware. Keep in lockstep with SPEC.md when these change.
constexpr float MOISTURE_THRESHOLD_PCT = 30.0f;
constexpr float MOISTURE_HYSTERESIS_PCT = 5.0f;
constexpr uint32_t MIN_WATERING_DURATION_SEC = 10;
constexpr uint32_t COOLDOWN_PERIOD_SEC = 900;

// Schedule constants -- a firmware-only layer on top of the moisture
// trigger, not part of SPEC.md's shared constants table. Placeholder
// values; tune once real garden hours/needs are known.
constexpr int SCHEDULE_WINDOW_START_HOUR = 6;   // 06:00 local, inclusive
constexpr int SCHEDULE_WINDOW_END_HOUR = 20;    // 20:00 local, exclusive
constexpr int MAX_WATERINGS_PER_DAY = 4;

// SPEC.md §3 Conventions
constexpr char DEVICE_ID[] = "zone_1";

// SPEC.md §1 MQTT Topics
constexpr char TOPIC_SENSOR_MOISTURE[] = "garden/sensor/moisture";
constexpr char TOPIC_PUMP_COMMAND[] = "garden/pump/command";
constexpr char TOPIC_PUMP_STATUS[] = "garden/pump/status";
constexpr char TOPIC_DEVICE_STATUS[] = "garden/device/status";
