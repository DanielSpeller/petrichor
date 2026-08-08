#pragma once

#include <cstdint>

// Runtime secrets (WiFi, MQTT broker) live in secrets.h. The repository
// ships secrets.h.example; copy it to secrets.h and fill in real values.
// secrets.h is gitignored and must never be committed.
#if __has_include("secrets.h")
    #include "secrets.h"
#else
    #error "Missing firmware/include/secrets.h — copy firmware/include/secrets.h.example and fill in your credentials."
#endif

#if __has_include("cloud_secrets.h")
    #include "cloud_secrets.h"
#else
    #error "Missing firmware/include/cloud_secrets.h — copy the cloud values from secrets.h.example."
#endif

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

// Local timezone as a POSIX TZ string. Used by currentLocalHour() for the
// schedule window. Example US Pacific: "PST8PDT,M3.2.0,M11.1.0".
// Default UTC means the schedule window is expressed in UTC; change this
// once the deployment timezone is known.
constexpr char TIMEZONE_POSIX[] = "UTC0";

constexpr uint32_t CHECK_INTERVAL_SEC = 600;
constexpr uint32_t CLOUD_SYNC_INTERVAL_SEC = 18000;
constexpr uint32_t CHECKS_PER_SYNC = CLOUD_SYNC_INTERVAL_SEC / CHECK_INTERVAL_SEC;

// SPEC.md §3 Conventions
constexpr char DEVICE_ID[] = "zone_1";

// SPEC.md §1 MQTT Topics
constexpr char TOPIC_SENSOR_MOISTURE[] = "garden/sensor/moisture";
constexpr char TOPIC_PUMP_COMMAND[] = "garden/pump/command";
constexpr char TOPIC_PUMP_STATUS[] = "garden/pump/status";
constexpr char TOPIC_PUMP_ACK[] = "garden/pump/ack";
constexpr char TOPIC_DEVICE_STATUS[] = "garden/device/status";
