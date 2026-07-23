# Firmware Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a PlatformIO/Arduino ESP32 firmware skeleton for the autonomous watering
system that is fully compilable and testable on a laptop with no hardware attached, with
every real-hardware dependency isolated behind one greppable, named function.

**Architecture:** A hardware-abstraction layer (`hal/`) provides mocked moisture-sensor and
pump-relay functions; pure-C++ decision logic (`watering_controller`, `schedule`) sits above
it with zero Arduino/network dependencies so it runs under PlatformIO's `native` test
environment; WiFi/MQTT/sleep/watchdog/device-status modules wire the ESP32-specific APIs and
only build under the `esp32dev` target. `main.cpp` wires everything together for the real
device build.

**Tech Stack:** PlatformIO, Arduino framework (ESP32), C++17, Unity test framework
(PlatformIO's built-in), PubSubClient (MQTT), local Mosquitto broker (dev dependency, not
exercisable without real hardware yet).

## Global Constraints

- MQTT topics/payloads/QoS/retained flags must match `SPEC.md` §1 exactly:
  `garden/sensor/moisture` (pub, QoS0, not retained), `garden/pump/command` (sub, QoS1, not
  retained), `garden/pump/status` (pub, QoS1, not retained), `garden/device/status` (pub,
  QoS0, retained).
- SPEC.md §4 shared constants (placeholder values, keep in lockstep with SPEC.md):
  `MOISTURE_THRESHOLD_PCT = 30`, `MOISTURE_HYSTERESIS_PCT = 5`,
  `MIN_WATERING_DURATION_SEC = 10`, `COOLDOWN_PERIOD_SEC = 900`.
- `device_id` is `"zone_1"` (SPEC.md §3), lowercase snake_case everywhere.
- Moisture is always a percentage 0-100, never raw ADC counts (SPEC.md §3).
- All timestamps are Unix epoch seconds, UTC (SPEC.md §3). All durations are integer seconds.
- Every real-hardware call (sensor read, relay write, WiFi connect, RSSI/battery read) must
  be isolated behind exactly one named, greppable function — nothing else in the firmware
  calls the underlying hardware API directly.
- No multi-device routing logic (SPEC.md scope note) — single device, `zone_1`, hardcoded.

---

### Task 1: Project scaffold and shared constants

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/include/config.h`

**Interfaces:**
- Produces: `MOISTURE_THRESHOLD_PCT` (float), `MOISTURE_HYSTERESIS_PCT` (float),
  `MIN_WATERING_DURATION_SEC` (uint32_t), `COOLDOWN_PERIOD_SEC` (uint32_t),
  `SCHEDULE_WINDOW_START_HOUR` (int), `SCHEDULE_WINDOW_END_HOUR` (int),
  `MAX_WATERINGS_PER_DAY` (int), `DEVICE_ID` (const char*), `TOPIC_SENSOR_MOISTURE`,
  `TOPIC_PUMP_COMMAND`, `TOPIC_PUMP_STATUS`, `TOPIC_DEVICE_STATUS` (const char*) — all
  consumed by every later task.

- [ ] **Step 1: Install PlatformIO Core**

Run: `pip install platformio`
Expected: installs successfully.

- [ ] **Step 2: Verify the install**

Run: `python -m platformio --version`
Expected: prints a PlatformIO Core version string (e.g. `PlatformIO Core, version 6.x.x`).

- [ ] **Step 3: Create the directory skeleton**

```bash
mkdir -p firmware/include firmware/src/hal firmware/test
```

- [ ] **Step 4: Write `firmware/platformio.ini`**

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    knolleary/PubSubClient@^2.8

[env:native]
platform = native
test_framework = unity
test_build_src = yes
build_src_filter =
    +<hal/moisture_sensor.cpp>
    +<hal/pump_relay.cpp>
    +<watering_controller.cpp>
    +<schedule.cpp>
    +<watchdog.cpp>
    +<wifi_manager.cpp>
```

`test_build_src = yes` is required — PlatformIO's `pio test` does not link `src/` into the
test binary by default, only `test/`. `build_src_filter` whitelists only the
Arduino-independent modules, so later tasks can add Arduino-only files (`clock.cpp`,
`mqtt_client.cpp`, etc.) to `src/` without breaking `pio test -e native` — entries for files
that don't exist yet (most of them, until Tasks 2-7 create them) are harmless no-ops, not
errors.

- [ ] **Step 5: Write `firmware/include/config.h`**

```cpp
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
```

- [ ] **Step 6: Commit**

```bash
git add firmware/platformio.ini firmware/include/config.h
git commit -m "firmware: scaffold PlatformIO project and shared constants"
```

---

### Task 2: Moisture sensor HAL (mocked, native-testable)

**Files:**
- Create: `firmware/src/hal/moisture_sensor.h`
- Create: `firmware/src/hal/moisture_sensor.cpp`
- Test: `firmware/test/test_moisture_sensor/test_main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `float readMoisturePercent()`, `void setScriptedMoistureSequence(const float* values, int count)` — consumed by `main.cpp` (Task 11) and usable by any test that needs deterministic moisture values.

- [ ] **Step 1: Write the failing test**

`firmware/test/test_moisture_sensor/test_main.cpp`:

```cpp
#include <unity.h>
#include "hal/moisture_sensor.h"

void setUp(void) {}
void tearDown(void) {}

void test_scripted_sequence_returns_values_in_order(void) {
    float sequence[] = {10.0f, 55.0f, 90.0f};
    setScriptedMoistureSequence(sequence, 3);

    TEST_ASSERT_EQUAL_FLOAT(10.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(90.0f, readMoisturePercent());
}

void test_scripted_sequence_repeats_last_value_once_exhausted(void) {
    float sequence[] = {42.0f};
    setScriptedMoistureSequence(sequence, 1);

    TEST_ASSERT_EQUAL_FLOAT(42.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(42.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(42.0f, readMoisturePercent());
}

void test_randomized_mock_stays_within_0_to_100(void) {
    setScriptedMoistureSequence(nullptr, 0); // revert to randomized mock
    for (int i = 0; i < 50; i++) {
        float value = readMoisturePercent();
        TEST_ASSERT_TRUE(value >= 0.0f && value <= 100.0f);
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_scripted_sequence_returns_values_in_order);
    RUN_TEST(test_scripted_sequence_repeats_last_value_once_exhausted);
    RUN_TEST(test_randomized_mock_stays_within_0_to_100);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_moisture_sensor`
Expected: FAIL to build — `hal/moisture_sensor.h` does not exist yet.

- [ ] **Step 3: Write `firmware/src/hal/moisture_sensor.h`**

```cpp
#pragma once

// Soil moisture HAL. Returns a percentage in [0.0, 100.0], matching
// SPEC.md's convention (never raw ADC counts).
//
// HARDWARE SWAP POINT: moisture_sensor.cpp currently returns mock values
// (scripted or randomized). When real hardware is available, replace the
// body of readMoisturePercent() with an analogRead(pin) call converted to
// a percentage -- nothing else in the firmware calls analogRead()
// directly, so this is the only function that needs to change. Note that
// doing so will pull in <Arduino.h>, so this file will no longer build
// under the `native` PlatformIO environment -- that's expected once real
// hardware exists.
float readMoisturePercent();

// Test-only hook. Makes readMoisturePercent() return values from `values`
// in order (repeating the last value once exhausted) instead of
// randomized mock values. Pass count == 0 (or values == nullptr) to
// revert to randomized mock values.
void setScriptedMoistureSequence(const float* values, int count);
```

- [ ] **Step 4: Write `firmware/src/hal/moisture_sensor.cpp`**

```cpp
#include "hal/moisture_sensor.h"

#include <cstdlib>

namespace {
const float* g_scriptedValues = nullptr;
int g_scriptedCount = 0;
int g_scriptedIndex = 0;
bool g_seeded = false;
}

void setScriptedMoistureSequence(const float* values, int count) {
    g_scriptedValues = values;
    g_scriptedCount = count;
    g_scriptedIndex = 0;
}

float readMoisturePercent() {
    if (g_scriptedValues != nullptr && g_scriptedCount > 0) {
        int index = g_scriptedIndex < g_scriptedCount ? g_scriptedIndex : g_scriptedCount - 1;
        float value = g_scriptedValues[index];
        if (g_scriptedIndex < g_scriptedCount) {
            g_scriptedIndex++;
        }
        return value;
    }

    if (!g_seeded) {
        std::srand(42); // fixed seed: deterministic mock run-to-run
        g_seeded = true;
    }
    return static_cast<float>(std::rand() % 10001) / 100.0f; // 0.00-100.00
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_moisture_sensor`
Expected: PASS — 3 tests, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/hal/moisture_sensor.h firmware/src/hal/moisture_sensor.cpp firmware/test/test_moisture_sensor
git commit -m "firmware: add mocked moisture sensor HAL"
```

---

### Task 3: Pump relay HAL (mocked, native-testable)

**Files:**
- Create: `firmware/src/hal/pump_relay.h`
- Create: `firmware/src/hal/pump_relay.cpp`
- Test: `firmware/test/test_pump_relay/test_main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `void setPumpRelay(bool on)`, `bool getPumpRelayState()` — consumed by `watchdog` (Task 9) and `main.cpp` (Task 11).

- [ ] **Step 1: Write the failing test**

`firmware/test/test_pump_relay/test_main.cpp`:

```cpp
#include <unity.h>
#include "hal/pump_relay.h"

void setUp(void) {}
void tearDown(void) {}

void test_relay_defaults_to_off(void) {
    TEST_ASSERT_FALSE(getPumpRelayState());
}

void test_relay_reflects_last_set_state(void) {
    setPumpRelay(true);
    TEST_ASSERT_TRUE(getPumpRelayState());

    setPumpRelay(false);
    TEST_ASSERT_FALSE(getPumpRelayState());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_relay_defaults_to_off);
    RUN_TEST(test_relay_reflects_last_set_state);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_pump_relay`
Expected: FAIL to build — `hal/pump_relay.h` does not exist yet.

- [ ] **Step 3: Write `firmware/src/hal/pump_relay.h`**

```cpp
#pragma once

// Pump relay HAL. `on == true` energizes the relay (pump running).
//
// HARDWARE SWAP POINT: pump_relay.cpp currently just tracks state in
// memory (mock). When real hardware is available, replace the body of
// setPumpRelay() with a digitalWrite(PUMP_RELAY_PIN, on ? HIGH : LOW)
// call -- nothing else in the firmware writes the pump pin directly, so
// this is the only function that needs to change.
void setPumpRelay(bool on);

// Current mock relay state. Used by watchdog logic and tests to assert
// the pump defaults to OFF.
bool getPumpRelayState();
```

- [ ] **Step 4: Write `firmware/src/hal/pump_relay.cpp`**

```cpp
#include "hal/pump_relay.h"

namespace {
bool g_relayOn = false; // safe default: OFF
}

void setPumpRelay(bool on) {
    g_relayOn = on;
}

bool getPumpRelayState() {
    return g_relayOn;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_pump_relay`
Expected: PASS — 2 tests, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/hal/pump_relay.h firmware/src/hal/pump_relay.cpp firmware/test/test_pump_relay
git commit -m "firmware: add mocked pump relay HAL"
```

---

### Task 4: Watering controller (threshold, hysteresis, cooldown)

**Files:**
- Create: `firmware/src/watering_controller.h`
- Create: `firmware/src/watering_controller.cpp`
- Test: `firmware/test/test_watering_controller/test_main.cpp`

**Interfaces:**
- Consumes: nothing (pure logic; takes moisture % and unix seconds as plain arguments).
- Produces: `enum class WaterDecision { WATER_TRIGGERED, NO_WATER_ABOVE_THRESHOLD, NO_WATER_HYSTERESIS_LOCKOUT, NO_WATER_COOLDOWN }`, class `WateringController` with constructor `WateringController(float thresholdPct, float hysteresisPct, uint32_t cooldownSec)`, method `WaterDecision evaluate(float moisturePct, uint32_t nowUnixSec)`, method `void notifyWateringComplete(uint32_t endTimeUnixSec)` — consumed by `schedule` tests (Task 5) and `main.cpp` (Task 11).

- [ ] **Step 1: Write the failing test**

`firmware/test/test_watering_controller/test_main.cpp`:

```cpp
#include <unity.h>
#include "watering_controller.h"

void setUp(void) {}
void tearDown(void) {}

void test_dry_soil_triggers_watering(void) {
    WateringController controller(30.0f, 5.0f, 900);
    WaterDecision decision = controller.evaluate(20.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED), static_cast<int>(decision));
}

void test_hysteresis_prevents_flapping_near_threshold(void) {
    WateringController controller(30.0f, 5.0f, 900);

    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED),
                       static_cast<int>(controller.evaluate(20.0f, 1000)));
    controller.notifyWateringComplete(1000);

    // Cooldown has fully elapsed, but moisture only recovered to 29% --
    // below the 35% rearm threshold -- so it must not retrigger.
    WaterDecision afterCooldown = controller.evaluate(29.0f, 1000 + 900 + 1);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_HYSTERESIS_LOCKOUT),
                       static_cast<int>(afterCooldown));

    // Moisture genuinely recovers past the rearm threshold (35%).
    WaterDecision rearmed = controller.evaluate(36.0f, 1000 + 900 + 100);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_ABOVE_THRESHOLD),
                       static_cast<int>(rearmed));

    // Now it dries out again -- should be able to trigger.
    WaterDecision retrigger = controller.evaluate(25.0f, 1000 + 900 + 200);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED),
                       static_cast<int>(retrigger));
}

void test_cooldown_blocks_second_trigger(void) {
    WateringController controller(30.0f, 5.0f, 900);

    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED),
                       static_cast<int>(controller.evaluate(20.0f, 1000)));
    controller.notifyWateringComplete(1000);

    // Moisture spikes well past rearm (soil got wet), then dries out again
    // quickly, inside the 900s cooldown window.
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_ABOVE_THRESHOLD),
                       static_cast<int>(controller.evaluate(80.0f, 1010)));

    WaterDecision withinCooldown = controller.evaluate(20.0f, 1200); // 200s after watering ended
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_COOLDOWN),
                       static_cast<int>(withinCooldown));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_dry_soil_triggers_watering);
    RUN_TEST(test_hysteresis_prevents_flapping_near_threshold);
    RUN_TEST(test_cooldown_blocks_second_trigger);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_watering_controller`
Expected: FAIL to build — `watering_controller.h` does not exist yet.

- [ ] **Step 3: Write `firmware/src/watering_controller.h`**

```cpp
#pragma once

#include <cstdint>

enum class WaterDecision {
    WATER_TRIGGERED,
    NO_WATER_ABOVE_THRESHOLD,
    NO_WATER_HYSTERESIS_LOCKOUT,
    NO_WATER_COOLDOWN
};

// Pure decision logic: threshold + hysteresis + cooldown. Has no
// Arduino/network dependency, which is what makes it testable under
// PlatformIO's `native` environment without any hardware.
class WateringController {
public:
    WateringController(float thresholdPct, float hysteresisPct, uint32_t cooldownSec);

    // Evaluates the current moisture reading against threshold, hysteresis
    // and cooldown rules. `nowUnixSec` is the current time (unix epoch
    // seconds). Does not mutate cooldown/hysteresis state by itself --
    // call notifyWateringComplete() when a watering actually happens.
    WaterDecision evaluate(float moisturePct, uint32_t nowUnixSec);

    // Call after a watering actually completes (not just triggered) so
    // cooldown and hysteresis lockout are tracked correctly. The caller
    // (main.cpp) is responsible for also checking the schedule before
    // acting on a WATER_TRIGGERED decision -- this class only knows about
    // moisture and time.
    void notifyWateringComplete(uint32_t endTimeUnixSec);

private:
    float thresholdPct_;
    float hysteresisPct_;
    uint32_t cooldownSec_;

    bool armed_ = true;
    bool hasWateredBefore_ = false;
    uint32_t lastWateringEndTime_ = 0;
};
```

- [ ] **Step 4: Write `firmware/src/watering_controller.cpp`**

```cpp
#include "watering_controller.h"

WateringController::WateringController(float thresholdPct, float hysteresisPct, uint32_t cooldownSec)
    : thresholdPct_(thresholdPct), hysteresisPct_(hysteresisPct), cooldownSec_(cooldownSec) {}

WaterDecision WateringController::evaluate(float moisturePct, uint32_t nowUnixSec) {
    if (!armed_) {
        if (moisturePct >= thresholdPct_ + hysteresisPct_) {
            armed_ = true;
        } else {
            return WaterDecision::NO_WATER_HYSTERESIS_LOCKOUT;
        }
    }

    if (moisturePct > thresholdPct_) {
        return WaterDecision::NO_WATER_ABOVE_THRESHOLD;
    }

    if (hasWateredBefore_ && (nowUnixSec - lastWateringEndTime_) < cooldownSec_) {
        return WaterDecision::NO_WATER_COOLDOWN;
    }

    return WaterDecision::WATER_TRIGGERED;
}

void WateringController::notifyWateringComplete(uint32_t endTimeUnixSec) {
    lastWateringEndTime_ = endTimeUnixSec;
    hasWateredBefore_ = true;
    armed_ = false;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_watering_controller`
Expected: PASS — 3 tests, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/watering_controller.h firmware/src/watering_controller.cpp firmware/test/test_watering_controller
git commit -m "firmware: add watering controller (threshold/hysteresis/cooldown)"
```

---

### Task 5: Schedule layer (window, max waterings/day)

**Files:**
- Create: `firmware/src/schedule.h`
- Create: `firmware/src/schedule.cpp`
- Test: `firmware/test/test_schedule/test_main.cpp`

**Interfaces:**
- Consumes: nothing directly, but its test file also uses `WateringController`/`WaterDecision` from Task 4 (`watering_controller.h`) to demonstrate the interaction rule.
- Produces: `struct ScheduleConfig { int windowStartHour; int windowEndHour; int maxWateringsPerDay; }`, class `Schedule` with constructor `explicit Schedule(const ScheduleConfig& config)`, methods `bool isWithinWindow(int localHour) const`, `bool isUnderDailyLimit(int wateringsSoFarToday) const`, `bool allowsWatering(int localHour, int wateringsSoFarToday) const` — consumed by `main.cpp` (Task 11).

- [ ] **Step 1: Write the failing test**

`firmware/test/test_schedule/test_main.cpp`:

```cpp
#include <unity.h>
#include "schedule.h"
#include "watering_controller.h"

void setUp(void) {}
void tearDown(void) {}

void test_within_window_and_under_limit_allows_watering(void) {
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    TEST_ASSERT_TRUE(schedule.allowsWatering(10, 1));
}

void test_outside_window_blocks_watering(void) {
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    TEST_ASSERT_FALSE(schedule.allowsWatering(22, 0));
}

void test_at_daily_limit_blocks_watering(void) {
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    TEST_ASSERT_FALSE(schedule.allowsWatering(10, 4));
}

void test_schedule_blocks_moisture_triggered_water_outside_window(void) {
    // The moisture controller wants to water (soil is dry)...
    WateringController controller(30.0f, 5.0f, 900);
    WaterDecision decision = controller.evaluate(20.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED), static_cast<int>(decision));

    // ...but it's outside the allowed window, so the schedule vetoes it.
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    bool allowed = schedule.allowsWatering(23, 0); // 11pm, outside 06:00-20:00
    TEST_ASSERT_FALSE(allowed);

    // main.cpp's rule: only actually command a watering if both the
    // moisture controller AND the schedule agree.
    bool shouldWaterNow = (decision == WaterDecision::WATER_TRIGGERED) && allowed;
    TEST_ASSERT_FALSE(shouldWaterNow);
}

void test_schedule_alone_never_forces_a_watering(void) {
    // Moisture is fine (not dry) -- even though the schedule window is
    // open and under the daily limit, nothing should trigger a watering.
    // The schedule can only veto a moisture trigger, never create one.
    WateringController controller(30.0f, 5.0f, 900);
    WaterDecision decision = controller.evaluate(60.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_ABOVE_THRESHOLD), static_cast<int>(decision));

    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    bool allowed = schedule.allowsWatering(10, 0); // well within window

    bool shouldWaterNow = (decision == WaterDecision::WATER_TRIGGERED) && allowed;
    TEST_ASSERT_FALSE(shouldWaterNow); // allowed=true doesn't matter; decision wasn't a trigger
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_within_window_and_under_limit_allows_watering);
    RUN_TEST(test_outside_window_blocks_watering);
    RUN_TEST(test_at_daily_limit_blocks_watering);
    RUN_TEST(test_schedule_blocks_moisture_triggered_water_outside_window);
    RUN_TEST(test_schedule_alone_never_forces_a_watering);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_schedule`
Expected: FAIL to build — `schedule.h` does not exist yet.

- [ ] **Step 3: Write `firmware/src/schedule.h`**

```cpp
#pragma once

struct ScheduleConfig {
    int windowStartHour; // local hour, 0-23, inclusive
    int windowEndHour;   // local hour, 0-23, exclusive
    int maxWateringsPerDay;
};

// Pure logic, no Arduino/network dependency -- testable under `native`.
class Schedule {
public:
    explicit Schedule(const ScheduleConfig& config);

    // True if `localHour` (0-23) falls inside the allowed watering window.
    bool isWithinWindow(int localHour) const;

    // True if `wateringsSoFarToday` is still under the daily cap.
    bool isUnderDailyLimit(int wateringsSoFarToday) const;

    // Combines both checks -- what main.cpp calls before acting on a
    // WaterDecision::WATER_TRIGGERED from WateringController. The
    // schedule can only veto a moisture trigger; it never creates one on
    // its own.
    bool allowsWatering(int localHour, int wateringsSoFarToday) const;

private:
    ScheduleConfig config_;
};
```

- [ ] **Step 4: Write `firmware/src/schedule.cpp`**

```cpp
#include "schedule.h"

Schedule::Schedule(const ScheduleConfig& config) : config_(config) {}

bool Schedule::isWithinWindow(int localHour) const {
    return localHour >= config_.windowStartHour && localHour < config_.windowEndHour;
}

bool Schedule::isUnderDailyLimit(int wateringsSoFarToday) const {
    return wateringsSoFarToday < config_.maxWateringsPerDay;
}

bool Schedule::allowsWatering(int localHour, int wateringsSoFarToday) const {
    return isWithinWindow(localHour) && isUnderDailyLimit(wateringsSoFarToday);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_schedule`
Expected: PASS — 5 tests, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/schedule.h firmware/src/schedule.cpp firmware/test/test_schedule
git commit -m "firmware: add schedule layer and moisture/schedule interaction tests"
```

---

### Task 6: WiFi manager (state machine, exponential backoff, mocked connection)

**Files:**
- Create: `firmware/src/wifi_manager.h`
- Create: `firmware/src/wifi_manager.cpp`
- Test: `firmware/test/test_wifi_manager/test_main.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum class WifiState { DISCONNECTED, CONNECTING, CONNECTED }`, class `WifiManager` with constructor `WifiManager(uint32_t initialBackoffMs, uint32_t maxBackoffMs)`, method `void update(uint32_t nowMs)`, method `WifiState state() const`; free functions `void wifiConnect()`, `bool wifiIsConnected()`, test hook `void setMockWifiShouldConnect(bool shouldConnect)` — consumed by `main.cpp` (Task 11).

- [ ] **Step 1: Write the failing test**

`firmware/test/test_wifi_manager/test_main.cpp`:

```cpp
#include <unity.h>
#include "wifi_manager.h"

void setUp(void) {
    setMockWifiShouldConnect(true); // reset to default before each test
}
void tearDown(void) {}

void test_connects_immediately_when_mock_wifi_available(void) {
    WifiManager manager(1000, 30000);
    manager.update(0);
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::CONNECTED), static_cast<int>(manager.state()));
}

void test_exponential_backoff_grows_after_failed_attempts(void) {
    setMockWifiShouldConnect(false);
    WifiManager manager(1000, 30000);

    manager.update(0);      // attempt #1 fails at t=0; next attempt at t=1000
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::DISCONNECTED), static_cast<int>(manager.state()));

    manager.update(500);    // too soon, must not attempt again yet
    manager.update(1000);   // attempt #2 fails at t=1000; backoff doubles to 2000ms, next at 3000
    manager.update(2999);   // too soon
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::DISCONNECTED), static_cast<int>(manager.state()));

    setMockWifiShouldConnect(true);
    manager.update(3000);   // attempt #3 at t=3000 succeeds
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::CONNECTED), static_cast<int>(manager.state()));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_connects_immediately_when_mock_wifi_available);
    RUN_TEST(test_exponential_backoff_grows_after_failed_attempts);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_wifi_manager`
Expected: FAIL to build — `wifi_manager.h` does not exist yet.

- [ ] **Step 3: Write `firmware/src/wifi_manager.h`**

```cpp
#pragma once

#include <cstdint>

enum class WifiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

// HARDWARE SWAP POINT: these two functions are the only place WiFi.begin()/
// WiFi.status() need to be called from. Currently mocked: wifiConnect()
// sets a mock "connected" flag, wifiIsConnected() reads it.
void wifiConnect();
bool wifiIsConnected();

// Test-only hook: forces wifiConnect() to simulate connection failure (or
// restores the default "always succeeds" mock behavior).
void setMockWifiShouldConnect(bool shouldConnect);

// Connection state machine with exponential backoff between attempts. Has
// no Arduino dependency itself (time is passed in), so it's testable
// under `native` even though real WiFi doesn't exist to test against yet.
class WifiManager {
public:
    WifiManager(uint32_t initialBackoffMs, uint32_t maxBackoffMs);

    // Call every loop() iteration with the current millis(). Drives the
    // DISCONNECTED -> CONNECTING -> CONNECTED state machine.
    void update(uint32_t nowMs);

    WifiState state() const;

private:
    void attemptConnect(uint32_t nowMs);

    WifiState state_ = WifiState::DISCONNECTED;
    uint32_t initialBackoffMs_;
    uint32_t maxBackoffMs_;
    uint32_t currentBackoffMs_;
    uint32_t nextAttemptAtMs_ = 0;
};
```

- [ ] **Step 4: Write `firmware/src/wifi_manager.cpp`**

```cpp
#include "wifi_manager.h"

namespace {
bool g_mockConnected = false;
bool g_mockShouldConnect = true;
}

void setMockWifiShouldConnect(bool shouldConnect) {
    g_mockShouldConnect = shouldConnect;
    if (!shouldConnect) {
        g_mockConnected = false;
    }
}

void wifiConnect() {
    // HARDWARE SWAP POINT: replace with WiFi.begin(ssid, password).
    g_mockConnected = g_mockShouldConnect;
}

bool wifiIsConnected() {
    // HARDWARE SWAP POINT: replace with (WiFi.status() == WL_CONNECTED).
    return g_mockConnected;
}

WifiManager::WifiManager(uint32_t initialBackoffMs, uint32_t maxBackoffMs)
    : initialBackoffMs_(initialBackoffMs),
      maxBackoffMs_(maxBackoffMs),
      currentBackoffMs_(initialBackoffMs) {}

void WifiManager::attemptConnect(uint32_t nowMs) {
    state_ = WifiState::CONNECTING;
    wifiConnect();
    if (wifiIsConnected()) {
        state_ = WifiState::CONNECTED;
        currentBackoffMs_ = initialBackoffMs_; // reset backoff on success
    } else {
        state_ = WifiState::DISCONNECTED;
        nextAttemptAtMs_ = nowMs + currentBackoffMs_;
        currentBackoffMs_ = currentBackoffMs_ * 2 < maxBackoffMs_ ? currentBackoffMs_ * 2 : maxBackoffMs_;
    }
}

void WifiManager::update(uint32_t nowMs) {
    if (state_ == WifiState::CONNECTED && !wifiIsConnected()) {
        state_ = WifiState::DISCONNECTED; // dropped connection
    }

    if (state_ == WifiState::DISCONNECTED && nowMs >= nextAttemptAtMs_) {
        attemptConnect(nowMs);
    }
}

WifiState WifiManager::state() const {
    return state_;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_wifi_manager`
Expected: PASS — 2 tests, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/wifi_manager.h firmware/src/wifi_manager.cpp firmware/test/test_wifi_manager
git commit -m "firmware: add WiFi state machine with exponential backoff"
```

---

### Task 7: Watchdog and safe-default-off

**Files:**
- Create: `firmware/src/watchdog.h`
- Create: `firmware/src/watchdog.cpp`
- Test: `firmware/test/test_watchdog/test_main.cpp`

**Interfaces:**
- Consumes: `setPumpRelay(bool)`, `getPumpRelayState()` from `hal/pump_relay.h` (Task 3).
- Produces: `void enforceSafeDefaultPumpOff()` (native-testable), `void setupWatchdog(uint32_t timeoutSec)` and `void feedWatchdog()` (esp32dev-only, guarded by `#ifdef ARDUINO`) — consumed by `main.cpp` (Task 11).

- [ ] **Step 1: Write the failing test**

`firmware/test/test_watchdog/test_main.cpp`:

```cpp
#include <unity.h>
#include "watchdog.h"
#include "hal/pump_relay.h"

void setUp(void) {}
void tearDown(void) {}

void test_enforce_safe_default_turns_pump_off(void) {
    setPumpRelay(true); // simulate pump left running from some prior state
    enforceSafeDefaultPumpOff();
    TEST_ASSERT_FALSE(getPumpRelayState());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_enforce_safe_default_turns_pump_off);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_watchdog`
Expected: FAIL to build — `watchdog.h` does not exist yet.

- [ ] **Step 3: Write `firmware/src/watchdog.h`**

```cpp
#pragma once

#include <cstdint>

// Forces the pump relay to its safe default (OFF). Call this at the very
// start of setup(), before anything else, so any reset or undefined state
// always leaves the pump off. Native-testable (only touches pump_relay).
void enforceSafeDefaultPumpOff();

// Configures the ESP32 hardware watchdog timer with the given timeout and
// enables panic-on-timeout, so an unresponsive main loop forces a reboot
// rather than leaving the pump in an unknown state.
//
// REQUIRES REAL HARDWARE: calls into esp_task_wdt; only builds under the
// esp32dev target (guarded by #ifdef ARDUINO).
void setupWatchdog(uint32_t timeoutSec);

// Feeds (resets) the watchdog. Call once per main loop iteration.
// REQUIRES REAL HARDWARE, same as setupWatchdog().
void feedWatchdog();
```

- [ ] **Step 4: Write `firmware/src/watchdog.cpp`**

```cpp
#include "watchdog.h"
#include "hal/pump_relay.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>
#endif

void enforceSafeDefaultPumpOff() {
    setPumpRelay(false);
}

#ifdef ARDUINO
void setupWatchdog(uint32_t timeoutSec) {
    esp_task_wdt_init(timeoutSec, true); // panic (reboot) on timeout
    esp_task_wdt_add(nullptr);           // watch the current task
}

void feedWatchdog() {
    esp_task_wdt_reset();
}
#endif
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_watchdog`
Expected: PASS — 1 test, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/watchdog.h firmware/src/watchdog.cpp firmware/test/test_watchdog
git commit -m "firmware: add watchdog setup and safe-default-off enforcement"
```

---

### Task 8: Clock HAL (mocked unix time / local hour, esp32dev only)

**Files:**
- Create: `firmware/src/hal/clock.h`
- Create: `firmware/src/hal/clock.cpp`

**Interfaces:**
- Consumes: `millis()` (Arduino).
- Produces: `uint32_t currentUnixTimeSec()`, `int currentLocalHour()` — consumed by `main.cpp` (Task 11). Not natively testable (depends on `millis()`); not added to `native` `build_src_filter`.

This module depends on `millis()`, so it only builds under `esp32dev`. There is no native
test for it — it's exercised implicitly by compiling `main.cpp` in Task 11.

- [ ] **Step 1: Write `firmware/src/hal/clock.h`**

```cpp
#pragma once

#include <cstdint>

// HARDWARE SWAP POINT: replace both bodies in clock.cpp with real
// NTP-synced time (e.g. via configTime() + time(nullptr)) when hardware
// exists. Currently mocked: unix time starts from a fixed epoch and
// advances with millis(); local hour is derived from that as UTC (no real
// timezone handling yet).
uint32_t currentUnixTimeSec();
int currentLocalHour();
```

- [ ] **Step 2: Write `firmware/src/hal/clock.cpp`**

```cpp
#include "hal/clock.h"
#include <Arduino.h>

namespace {
constexpr uint32_t MOCK_START_UNIX_TIME = 1753277940; // arbitrary fixed epoch for mock runs
}

uint32_t currentUnixTimeSec() {
    // HARDWARE SWAP POINT: replace with real NTP-synced time.
    return MOCK_START_UNIX_TIME + (millis() / 1000);
}

int currentLocalHour() {
    return static_cast<int>((currentUnixTimeSec() / 3600) % 24);
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware/src/hal/clock.h firmware/src/hal/clock.cpp
git commit -m "firmware: add mocked clock HAL for unix time / local hour"
```

---

### Task 9: Device status heartbeat builder (esp32dev only)

**Files:**
- Create: `firmware/src/device_status.h`
- Create: `firmware/src/device_status.cpp`

**Interfaces:**
- Consumes: `millis()` (Arduino).
- Produces: `int readWifiRssiDbm()`, `float readBatteryVoltageV()`, `uint32_t readUptimeSec()` — consumed by `main.cpp` (Task 11). Not natively testable (depends on `millis()`); not added to `native` `build_src_filter`.

- [ ] **Step 1: Write `firmware/src/device_status.h`**

```cpp
#pragma once

#include <cstdint>

// Heartbeat data for garden/device/status (SPEC.md §1). Values are read
// via these functions so main.cpp doesn't need to know whether they're
// real or mocked.
//
// HARDWARE SWAP POINT: replace the bodies of readWifiRssiDbm() and
// readBatteryVoltageV() in device_status.cpp with WiFi.RSSI() and a real
// battery ADC read when hardware exists. readUptimeSec() is already real
// (millis()-based) and needs no swap.
int readWifiRssiDbm();
float readBatteryVoltageV();
uint32_t readUptimeSec();
```

- [ ] **Step 2: Write `firmware/src/device_status.cpp`**

```cpp
#include "device_status.h"
#include <Arduino.h>

int readWifiRssiDbm() {
    // HARDWARE SWAP POINT: replace with WiFi.RSSI().
    return -60; // mocked, plausible mid-strength signal
}

float readBatteryVoltageV() {
    // HARDWARE SWAP POINT: replace with a real ADC read + voltage divider math.
    return 3.98f; // mocked, plausible LiPo voltage
}

uint32_t readUptimeSec() {
    return millis() / 1000;
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware/src/device_status.h firmware/src/device_status.cpp
git commit -m "firmware: add device status heartbeat HAL"
```

---

### Task 10: Deep sleep scaffold (esp32dev only, needs real-device testing)

**Files:**
- Create: `firmware/src/sleep_manager.h`
- Create: `firmware/src/sleep_manager.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `void enterDeepSleep(uint32_t sleepDurationSec)` — available to `main.cpp` but NOT called from the Task 11 `loop()` (deep sleep would stop the mocked main loop from being observable/iterable during remote-prep; wiring it into the real wake/sleep cycle is real-hardware work, called out below).

- [ ] **Step 1: Write `firmware/src/sleep_manager.h`**

```cpp
#pragma once

#include <cstdint>

// Deep sleep wake/sleep cycle scaffold.
//
// NEEDS REAL-DEVICE TESTING: deep sleep behavior (timer wake source,
// current draw, GPIO state retention across sleep) can only be
// meaningfully verified on real ESP32 hardware. This function is correct
// per the ESP32 Arduino core's documented API, but untested here -- it is
// intentionally not called from main.cpp's loop() yet, since doing so
// would stop the mocked main loop from being observable during
// remote-prep. Wire it in once real hardware exists and the wake/sleep
// cadence has been decided.
void enterDeepSleep(uint32_t sleepDurationSec);
```

- [ ] **Step 2: Write `firmware/src/sleep_manager.cpp`**

```cpp
#include "sleep_manager.h"
#include <esp_sleep.h>
#include <Arduino.h>

void enterDeepSleep(uint32_t sleepDurationSec) {
    // NEEDS REAL-DEVICE TESTING -- see header comment.
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleepDurationSec) * 1000000ULL);
    esp_deep_sleep_start();
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware/src/sleep_manager.h firmware/src/sleep_manager.cpp
git commit -m "firmware: add deep sleep scaffold (needs real-device testing)"
```

---

### Task 11: MQTT client (wraps PubSubClient, esp32dev only)

**Files:**
- Create: `firmware/src/mqtt_client.h`
- Create: `firmware/src/mqtt_client.cpp`

**Interfaces:**
- Consumes: `config.h` constants (Task 1); `PubSubClient`/`Client` from the `PubSubClient` library.
- Produces: class `MqttClient` with constructor `MqttClient(Client& networkClient, const char* brokerHost, uint16_t brokerPort)`, methods `bool connect(const char* clientId)`, `void loop()`, `bool isConnected()`, `bool publishMoistureReading(float moisturePct, uint32_t timestamp)`, `bool publishPumpStatus(const char* requestId, const char* trigger, const char* result, uint32_t requestedDurationSec, uint32_t actualDurationSec, float moistureBeforePct, uint32_t timestamp)`, `bool publishDeviceStatus(int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec, uint32_t timestamp)`, `bool subscribeToPumpCommand(MQTT_CALLBACK_SIGNATURE)` — consumed by `main.cpp` (Task 12).

This module requires a real `WiFiClient`/TCP stack and only builds under `esp32dev` — it is
not natively testable and not added to the `native` `build_src_filter`. Verification for this
task is a compile check only (no hardware, no broker connection possible yet).

- [ ] **Step 1: Write `firmware/src/mqtt_client.h`**

```cpp
#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

// Thin wrapper around PubSubClient publishing/subscribing on the exact
// topics and payload shapes defined in SPEC.md §1. REQUIRES REAL HARDWARE
// (WiFiClient/TCP) to actually connect to a broker -- only builds under
// the esp32dev environment.
//
// KNOWN LIMITATION: PubSubClient always sends publishes at QoS 0 on the
// wire, regardless of the QoS SPEC.md specifies (QoS 1 for
// garden/pump/command and garden/pump/status). Subscriptions can request
// QoS 1 in the SUBSCRIBE packet, but true at-least-once publish
// guarantees will need either a different MQTT library or
// application-level ack handling once real hardware exists. Documented
// here so it isn't silently forgotten.
class MqttClient {
public:
    MqttClient(Client& networkClient, const char* brokerHost, uint16_t brokerPort);

    bool connect(const char* clientId);
    void loop();
    bool isConnected();

    // Publishes a garden/sensor/moisture reading. Not retained, per SPEC.md.
    bool publishMoistureReading(float moisturePct, uint32_t timestamp);

    // Publishes a garden/pump/status confirmation. Not retained, per SPEC.md.
    bool publishPumpStatus(const char* requestId, const char* trigger, const char* result,
                            uint32_t requestedDurationSec, uint32_t actualDurationSec,
                            float moistureBeforePct, uint32_t timestamp);

    // Publishes a garden/device/status heartbeat. Retained, per SPEC.md.
    bool publishDeviceStatus(int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec, uint32_t timestamp);

    // Subscribes to garden/pump/command with the given callback.
    bool subscribeToPumpCommand(MQTT_CALLBACK_SIGNATURE);

private:
    PubSubClient client_;
};
```

- [ ] **Step 2: Write `firmware/src/mqtt_client.cpp`**

```cpp
#include "mqtt_client.h"
#include "config.h"

#include <cstdio>

MqttClient::MqttClient(Client& networkClient, const char* brokerHost, uint16_t brokerPort)
    : client_(networkClient) {
    client_.setServer(brokerHost, brokerPort);
}

bool MqttClient::connect(const char* clientId) {
    return client_.connect(clientId);
}

void MqttClient::loop() {
    client_.loop();
}

bool MqttClient::isConnected() {
    return client_.connected();
}

bool MqttClient::publishMoistureReading(float moisturePct, uint32_t timestamp) {
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"moisture_pct\":%.1f,\"timestamp\":%lu}",
             DEVICE_ID, moisturePct, static_cast<unsigned long>(timestamp));
    return client_.publish(TOPIC_SENSOR_MOISTURE, payload, false);
}

bool MqttClient::publishPumpStatus(const char* requestId, const char* trigger, const char* result,
                                    uint32_t requestedDurationSec, uint32_t actualDurationSec,
                                    float moistureBeforePct, uint32_t timestamp) {
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"request_id\":\"%s\",\"trigger\":\"%s\",\"result\":\"%s\","
             "\"requested_duration_sec\":%lu,\"actual_duration_sec\":%lu,"
             "\"moisture_before_pct\":%.1f,\"timestamp\":%lu}",
             DEVICE_ID, requestId, trigger, result,
             static_cast<unsigned long>(requestedDurationSec),
             static_cast<unsigned long>(actualDurationSec),
             moistureBeforePct, static_cast<unsigned long>(timestamp));
    return client_.publish(TOPIC_PUMP_STATUS, payload, false);
}

bool MqttClient::publishDeviceStatus(int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec, uint32_t timestamp) {
    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"wifi_rssi_dbm\":%d,\"battery_voltage_v\":%.2f,"
             "\"uptime_sec\":%lu,\"timestamp\":%lu}",
             DEVICE_ID, wifiRssiDbm, batteryVoltageV,
             static_cast<unsigned long>(uptimeSec), static_cast<unsigned long>(timestamp));
    return client_.publish(TOPIC_DEVICE_STATUS, payload, true); // retained, per SPEC.md
}

bool MqttClient::subscribeToPumpCommand(MQTT_CALLBACK_SIGNATURE) {
    client_.setCallback(callback);
    return client_.subscribe(TOPIC_PUMP_COMMAND, 1);
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware/src/mqtt_client.h firmware/src/mqtt_client.cpp
git commit -m "firmware: add MQTT client wrapping PubSubClient per SPEC.md topics"
```

---

### Task 12: main.cpp wiring, README, and full verification

**Files:**
- Create: `firmware/src/main.cpp`
- Modify: `firmware/README.md`

**Interfaces:**
- Consumes: everything from Tasks 1-11 (`config.h`, `hal/moisture_sensor.h`, `hal/pump_relay.h`, `hal/clock.h`, `watering_controller.h`, `schedule.h`, `wifi_manager.h`, `mqtt_client.h`, `device_status.h`, `watchdog.h`).
- Produces: the Arduino `setup()`/`loop()` entry point. Nothing consumes this — it's the top of the dependency graph.

- [ ] **Step 1: Write `firmware/src/main.cpp`**

```cpp
#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hal/moisture_sensor.h"
#include "hal/pump_relay.h"
#include "hal/clock.h"
#include "watering_controller.h"
#include "schedule.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "device_status.h"
#include "watchdog.h"

namespace {
WifiManager g_wifiManager(1000, 30000);
WiFiClient g_networkClient;
// Local Mosquitto broker address -- update to match your laptop's IP/hostname.
MqttClient g_mqtt(g_networkClient, "192.168.1.100", 1883);
WateringController g_wateringController(MOISTURE_THRESHOLD_PCT, MOISTURE_HYSTERESIS_PCT, COOLDOWN_PERIOD_SEC);
Schedule g_schedule(ScheduleConfig{SCHEDULE_WINDOW_START_HOUR, SCHEDULE_WINDOW_END_HOUR, MAX_WATERINGS_PER_DAY});

int g_wateringsToday = 0;
int g_lastCountedDay = -1;

uint32_t g_lastHeartbeatMs = 0;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 60000;

uint32_t g_lastMoistureReadMs = 0;
constexpr uint32_t MOISTURE_READ_INTERVAL_MS = 10000;

void onPumpCommand(char* topic, uint8_t* payload, unsigned int length) {
    // Minimal stub for the remote-prep phase: no hardware exists to
    // actually act on a run/stop command yet, and JSON parsing needs a
    // library (e.g. ArduinoJson) not yet added as a dependency. Full
    // command handling (device_id match, run/stop, duration_sec) is a
    // follow-up once real hardware/broker testing begins.
    (void)topic;
    (void)payload;
    (void)length;
}
}

void setup() {
    Serial.begin(115200);
    enforceSafeDefaultPumpOff();
    setupWatchdog(30);
}

void loop() {
    feedWatchdog();

    uint32_t nowMs = millis();
    g_wifiManager.update(nowMs);

    if (g_wifiManager.state() == WifiState::CONNECTED && !g_mqtt.isConnected()) {
        g_mqtt.connect(DEVICE_ID);
        g_mqtt.subscribeToPumpCommand(onPumpCommand);
    }
    if (g_mqtt.isConnected()) {
        g_mqtt.loop();
    }

    uint32_t nowSec = currentUnixTimeSec();
    int today = static_cast<int>(nowSec / 86400);
    if (today != g_lastCountedDay) {
        g_wateringsToday = 0;
        g_lastCountedDay = today;
    }

    if (nowMs - g_lastMoistureReadMs >= MOISTURE_READ_INTERVAL_MS) {
        g_lastMoistureReadMs = nowMs;
        float moisturePct = readMoisturePercent();

        if (g_mqtt.isConnected()) {
            g_mqtt.publishMoistureReading(moisturePct, nowSec);
        }

        WaterDecision decision = g_wateringController.evaluate(moisturePct, nowSec);
        if (decision == WaterDecision::WATER_TRIGGERED &&
            g_schedule.allowsWatering(currentLocalHour(), g_wateringsToday)) {
            // SIMPLIFICATION: this blocks for the run duration, so an
            // incoming "stop" command can't interrupt it and MQTT/watchdog
            // servicing pauses during the run. A non-blocking state
            // machine (track pump-start time, check elapsed each loop) is
            // the natural next step once hardware exists and "stop early"
            // needs to actually work.
            setPumpRelay(true);
            delay(MIN_WATERING_DURATION_SEC * 1000UL);
            setPumpRelay(false);

            uint32_t completedAt = currentUnixTimeSec();
            g_wateringController.notifyWateringComplete(completedAt);
            g_wateringsToday++;

            if (g_mqtt.isConnected()) {
                g_mqtt.publishPumpStatus("local-trigger", "moisture", "completed",
                                          MIN_WATERING_DURATION_SEC, MIN_WATERING_DURATION_SEC,
                                          moisturePct, completedAt);
            }
        }
    }

    if (nowMs - g_lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
        g_lastHeartbeatMs = nowMs;
        if (g_mqtt.isConnected()) {
            g_mqtt.publishDeviceStatus(readWifiRssiDbm(), readBatteryVoltageV(), readUptimeSec(), currentUnixTimeSec());
        }
    }
}
```

- [ ] **Step 2: Compile the esp32dev target (no hardware needed for a build check)**

Run: `cd firmware && python -m platformio run -e esp32dev`
Expected: `SUCCESS` — PlatformIO downloads the ESP32 toolchain/libraries on first run and compiles cleanly. No board needs to be attached; this only compiles, it doesn't upload.

- [ ] **Step 3: Run the full native test suite**

Run: `cd firmware && python -m platformio test -e native`
Expected: PASS — all 5 native test suites (`test_moisture_sensor`, `test_pump_relay`, `test_watering_controller`, `test_schedule`, `test_wifi_manager`, `test_watchdog`) report 0 failures.

- [ ] **Step 4: Update `firmware/README.md`**

```markdown
# firmware

ESP32 firmware: moisture sensor + relay-controlled pump, talking MQTT to the Pi.

See `/SPEC.md` before changing any MQTT payload or DB field.

## Remote-prep phase (no hardware yet)

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

`sleep_manager.cpp`'s `enterDeepSleep()` is structured per the ESP32 Arduino core's
documented API but **needs real-device testing** — it's not wired into `main.cpp`'s loop yet.

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

Run it locally with `mosquitto -v`, and update the broker address in `src/main.cpp`
(`g_mqtt` constructor) to match your machine's IP.

Known limitation: PubSubClient (the MQTT library used here) always publishes at QoS 0 on the
wire, even though `SPEC.md` specifies QoS 1 for `garden/pump/command` and
`garden/pump/status`. Revisit this (different library or app-level acks) once real hardware
testing begins.
```

- [ ] **Step 5: Commit**

```bash
git add firmware/src/main.cpp firmware/README.md
git commit -m "firmware: wire main.cpp and document hardware swap points"
```
