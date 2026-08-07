# Firmware Standalone Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restructure the ESP32 firmware from a continuously-running loop into a battery-friendly deep-sleep cycle that checks/waters frequently and syncs to MQTT (dormant) and a new Cloudflare ingest endpoint infrequently.

**Architecture:** `setup()` runs the entire cycle once per wake (deep sleep resets the device, so `loop()` is never reached). Every wake: read moisture, run the existing pure decision logic, water if needed, buffer the reading. Every `CHECKS_PER_SYNC`-th wake: also bring WiFi up, resync time, opportunistically publish to MQTT, and POST buffered data to the Cloudflare Worker built in the companion plan (`docs/superpowers/plans/2026-08-07-cloudflare-ingest-service.md`). A cycle counter and small fixed-capacity buffers live in ESP32 RTC memory so they survive deep sleep.

**Tech Stack:** C++ (Arduino/ESP32 framework via PlatformIO), Unity test framework for the `native` host-test target.

## Global Constraints

- Every real-hardware call stays isolated behind a named function marked `HARDWARE SWAP POINT`, per the existing `firmware/README.md` convention. Don't add new unguarded hardware calls.
- Pure decision/data logic (no Arduino/network dependency) goes in the `native`-testable `build_src_filter` list in `firmware/platformio.ini`; hardware-only code (HTTP, MQTT, WiFi) stays out of it, matching how `mqtt_client.cpp` is already excluded.
- Any global variable that must survive an ESP32 deep-sleep wake is declared `RTC_DATA_ATTR` in `main.cpp` **and must be a plain aggregate with no constructor** — a non-trivial constructor reruns on every wake (deep sleep is a full reboot) and silently discards the preserved value. Use `= {}` / aggregate initialization at the declaration site instead.
- `SPEC.md` is the cross-language contract (firmware C++ ⇄ Cloudflare Worker TypeScript, same as it already is for firmware ⇄ Pi). Any new field name or JSON shape crossing that boundary gets documented there in the same change, per the file's own header rule.
- All moisture is 0-100 float percent, never raw ADC. All timestamps are Unix epoch seconds UTC. All durations are seconds. (`SPEC.md` §3, unchanged, still applies.)

---

### Task 1: Document the cloud ingest contract in `SPEC.md`

**Files:**
- Modify: `SPEC.md` (insert new `## 5. Cloud Ingest Endpoint` section before the existing `## Related` section, after `## 4. Shared Constants`)

**Interfaces:**
- Produces: the JSON shape both this plan (firmware sender) and the companion Cloudflare plan (receiver) build against — `device_id`, `timestamp`, `readings[]`, `watering_events[]`, `device_status{}` fields, all reusing existing `SPEC.md` §1/§2 field names.

- [ ] **Step 1: Insert the new section**

Insert this text into `SPEC.md`, immediately before the line `## Related`:

```markdown
## 5. Cloud Ingest Endpoint (standalone deployment)

Added for the standalone ESP32 deployment (see
`docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md`). This is a second
contract, alongside §1's MQTT topics — the two are independent and both may be active at
once (MQTT stays dormant, ready for a future Pi). Reuses the exact field names and
semantics from §1 and the §2 schema so nothing drifts between the two paths.

`POST https://<worker-host>/ingest`

- **Auth:** `Authorization: Bearer <CLOUD_SHARED_SECRET>` header. The secret is configured
  in `firmware/include/secrets.h` (device side) and as a Worker secret (`wrangler secret
  put CLOUD_SHARED_SECRET`, cloud side). Missing/incorrect header → `401`.
- **Body:** JSON, UTF-8. Sent once per cloud-sync wake (roughly every
  `CLOUD_SYNC_INTERVAL_SEC`), batching everything accumulated since the last successful
  sync.

| Field | Type | Notes |
|---|---|---|
| `device_id` | string | Same convention as §3. |
| `timestamp` | integer | Unix epoch seconds UTC, when this batch was sent. |
| `readings` | array of `{moisture_pct, timestamp}` | Same field shapes as §1's `garden/sensor/moisture` payload. May be empty. |
| `watering_events` | array of `{request_id, trigger, result, requested_duration_sec, actual_duration_sec, moisture_before_pct, timestamp}` | Same field shapes as §1's `garden/pump/status` payload. May be empty. |
| `device_status` | object `{wifi_rssi_dbm, battery_voltage_v, uptime_sec}` | Same field shapes as §1's `garden/device/status` payload (minus `device_id`/`timestamp`, already at top level). |

```json
{
  "device_id": "zone_1",
  "timestamp": 1753277970,
  "readings": [
    {"moisture_pct": 42.5, "timestamp": 1753277940}
  ],
  "watering_events": [
    {
      "request_id": "local_7",
      "trigger": "moisture",
      "result": "completed",
      "requested_duration_sec": 10,
      "actual_duration_sec": 10,
      "moisture_before_pct": 28.0,
      "timestamp": 1753277961
    }
  ],
  "device_status": {
    "wifi_rssi_dbm": -62,
    "battery_voltage_v": 3.98,
    "uptime_sec": 86412
  }
}
```

**Response:** `200 {"ok": true}` on success. `400` on malformed body. `401` on auth
failure.

**Server behavior:** each `readings[]` entry becomes a `readings` row (`received_at` =
server time of the request); each `watering_events[]` entry becomes a `watering_events`
row with `status` set from `result`; `device_status` is upserted by `device_id`, same as
§2's notes.

---

```

- [ ] **Step 2: Commit**

```bash
git add SPEC.md
git commit -m "docs: add SPEC.md §5 cloud ingest contract for standalone deployment"
```

---

### Task 2: `ReadingBuffer` — fixed-capacity moisture reading buffer

**Files:**
- Create: `firmware/src/reading_buffer.h`
- Create: `firmware/src/reading_buffer.cpp`
- Test: `firmware/test/test_reading_buffer/test_main.cpp`
- Modify: `firmware/platformio.ini` (add `+<reading_buffer.cpp>` to `env:native`'s `build_src_filter`)

**Interfaces:**
- Produces: `constexpr size_t READING_BUFFER_CAPACITY`; `struct ReadingBuffer { float moisturePct[READING_BUFFER_CAPACITY]; uint32_t timestampSec[READING_BUFFER_CAPACITY]; size_t count; }`; `void readingBufferPush(ReadingBuffer&, float moisturePct, uint32_t timestampSec)`; `void readingBufferClear(ReadingBuffer&)`.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_reading_buffer/test_main.cpp`:

```cpp
#include <unity.h>
#include "reading_buffer.h"

void setUp(void) {}
void tearDown(void) {}

void test_push_increments_count(void) {
    ReadingBuffer buffer = {};
    readingBufferPush(buffer, 42.5f, 1000);
    TEST_ASSERT_EQUAL(1, static_cast<int>(buffer.count));
    TEST_ASSERT_EQUAL_FLOAT(42.5f, buffer.moisturePct[0]);
    TEST_ASSERT_EQUAL_UINT32(1000, buffer.timestampSec[0]);
}

void test_push_multiple_preserves_order(void) {
    ReadingBuffer buffer = {};
    readingBufferPush(buffer, 10.0f, 100);
    readingBufferPush(buffer, 20.0f, 200);
    readingBufferPush(buffer, 30.0f, 300);
    TEST_ASSERT_EQUAL(3, static_cast<int>(buffer.count));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, buffer.moisturePct[0]);
    TEST_ASSERT_EQUAL_FLOAT(30.0f, buffer.moisturePct[2]);
}

void test_push_beyond_capacity_drops_oldest(void) {
    ReadingBuffer buffer = {};
    for (size_t i = 0; i < READING_BUFFER_CAPACITY; ++i) {
        readingBufferPush(buffer, static_cast<float>(i), static_cast<uint32_t>(i));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(READING_BUFFER_CAPACITY), static_cast<int>(buffer.count));

    readingBufferPush(buffer, 999.0f, 999);
    TEST_ASSERT_EQUAL(static_cast<int>(READING_BUFFER_CAPACITY), static_cast<int>(buffer.count));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, buffer.moisturePct[0]);
    TEST_ASSERT_EQUAL_FLOAT(999.0f, buffer.moisturePct[READING_BUFFER_CAPACITY - 1]);
}

void test_clear_resets_count(void) {
    ReadingBuffer buffer = {};
    readingBufferPush(buffer, 1.0f, 1);
    readingBufferClear(buffer);
    TEST_ASSERT_EQUAL(0, static_cast<int>(buffer.count));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_push_increments_count);
    RUN_TEST(test_push_multiple_preserves_order);
    RUN_TEST(test_push_beyond_capacity_drops_oldest);
    RUN_TEST(test_clear_resets_count);
    return UNITY_END();
}
```

- [ ] **Step 2: Create the header**

Create `firmware/src/reading_buffer.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

// Sized generously above CHECKS_PER_SYNC (config.h) so a normal sync cycle
// never fills it; the ring-drop behavior below is a fallback for repeated
// missed syncs, not the expected path.
constexpr size_t READING_BUFFER_CAPACITY = 40;

// Plain aggregate (no constructor) so it's safe to declare RTC_DATA_ATTR in
// main.cpp: a type with a non-trivial constructor would have that
// constructor rerun by the C++ runtime on every ESP32 deep-sleep wake
// (which reboots and reruns global initializers), silently discarding the
// RTC-preserved contents. Zero-init at the declaration site instead:
// `ReadingBuffer buf = {};`.
struct ReadingBuffer {
    float moisturePct[READING_BUFFER_CAPACITY];
    uint32_t timestampSec[READING_BUFFER_CAPACITY];
    size_t count;
};

// Appends a reading. If the buffer is full, the oldest entry is dropped to
// make room (ring behavior) -- losing one old sample is preferable to
// losing the newest one or crashing.
void readingBufferPush(ReadingBuffer& buffer, float moisturePct, uint32_t timestampSec);

void readingBufferClear(ReadingBuffer& buffer);
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_reading_buffer`
Expected: FAIL/build error — `readingBufferPush`/`readingBufferClear` undefined.

- [ ] **Step 4: Implement**

Create `firmware/src/reading_buffer.cpp`:

```cpp
#include "reading_buffer.h"

void readingBufferPush(ReadingBuffer& buffer, float moisturePct, uint32_t timestampSec) {
    if (buffer.count < READING_BUFFER_CAPACITY) {
        buffer.moisturePct[buffer.count] = moisturePct;
        buffer.timestampSec[buffer.count] = timestampSec;
        buffer.count++;
        return;
    }
    for (size_t i = 1; i < READING_BUFFER_CAPACITY; ++i) {
        buffer.moisturePct[i - 1] = buffer.moisturePct[i];
        buffer.timestampSec[i - 1] = buffer.timestampSec[i];
    }
    buffer.moisturePct[READING_BUFFER_CAPACITY - 1] = moisturePct;
    buffer.timestampSec[READING_BUFFER_CAPACITY - 1] = timestampSec;
}

void readingBufferClear(ReadingBuffer& buffer) {
    buffer.count = 0;
}
```

- [ ] **Step 5: Add to the native test build filter**

In `firmware/platformio.ini`, under `[env:native]`'s `build_src_filter`, add a line:

```ini
    +<reading_buffer.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_reading_buffer`
Expected: PASS, 4 tests.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/reading_buffer.h firmware/src/reading_buffer.cpp firmware/test/test_reading_buffer firmware/platformio.ini
git commit -m "firmware: add RTC-safe ReadingBuffer for batched cloud/MQTT sync"
```

---

### Task 3: `WateringEventBuffer` — fixed-capacity watering event buffer

**Files:**
- Create: `firmware/src/watering_event_buffer.h`
- Create: `firmware/src/watering_event_buffer.cpp`
- Test: `firmware/test/test_watering_event_buffer/test_main.cpp`
- Modify: `firmware/platformio.ini` (add `+<watering_event_buffer.cpp>`)

**Interfaces:**
- Produces: `constexpr size_t WATERING_EVENT_BUFFER_CAPACITY`; `struct WateringEventRecord { char requestId[37]; char trigger[16]; char result[16]; uint32_t requestedDurationSec; uint32_t actualDurationSec; float moistureBeforePct; uint32_t timestampSec; }`; `struct WateringEventBuffer { WateringEventRecord events[WATERING_EVENT_BUFFER_CAPACITY]; size_t count; }`; `void wateringEventBufferPush(WateringEventBuffer&, const WateringEventRecord&)`; `void wateringEventBufferClear(WateringEventBuffer&)`.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_watering_event_buffer/test_main.cpp`:

```cpp
#include <cstring>
#include <unity.h>
#include "watering_event_buffer.h"

void setUp(void) {}
void tearDown(void) {}

WateringEventRecord makeRecord(const char* requestId, uint32_t timestampSec) {
    WateringEventRecord record = {};
    strncpy(record.requestId, requestId, sizeof(record.requestId) - 1);
    strncpy(record.trigger, "moisture", sizeof(record.trigger) - 1);
    strncpy(record.result, "completed", sizeof(record.result) - 1);
    record.requestedDurationSec = 10;
    record.actualDurationSec = 10;
    record.moistureBeforePct = 28.0f;
    record.timestampSec = timestampSec;
    return record;
}

void test_push_increments_count(void) {
    WateringEventBuffer buffer = {};
    wateringEventBufferPush(buffer, makeRecord("local_1", 1000));
    TEST_ASSERT_EQUAL(1, static_cast<int>(buffer.count));
    TEST_ASSERT_EQUAL_STRING("local_1", buffer.events[0].requestId);
}

void test_push_beyond_capacity_drops_oldest(void) {
    WateringEventBuffer buffer = {};
    char idBuf[16];
    for (size_t i = 0; i < WATERING_EVENT_BUFFER_CAPACITY; ++i) {
        snprintf(idBuf, sizeof(idBuf), "req-%d", static_cast<int>(i));
        wateringEventBufferPush(buffer, makeRecord(idBuf, static_cast<uint32_t>(i)));
    }
    wateringEventBufferPush(buffer, makeRecord("req-last", 999));

    TEST_ASSERT_EQUAL(static_cast<int>(WATERING_EVENT_BUFFER_CAPACITY), static_cast<int>(buffer.count));
    TEST_ASSERT_EQUAL_STRING("req-1", buffer.events[0].requestId);
    TEST_ASSERT_EQUAL_STRING("req-last", buffer.events[WATERING_EVENT_BUFFER_CAPACITY - 1].requestId);
}

void test_clear_resets_count(void) {
    WateringEventBuffer buffer = {};
    wateringEventBufferPush(buffer, makeRecord("local_1", 1000));
    wateringEventBufferClear(buffer);
    TEST_ASSERT_EQUAL(0, static_cast<int>(buffer.count));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_push_increments_count);
    RUN_TEST(test_push_beyond_capacity_drops_oldest);
    RUN_TEST(test_clear_resets_count);
    return UNITY_END();
}
```

- [ ] **Step 2: Create the header**

Create `firmware/src/watering_event_buffer.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t WATERING_EVENT_BUFFER_CAPACITY = 8;
constexpr size_t WATERING_EVENT_MAX_REQUEST_ID_LEN = 36;
constexpr size_t WATERING_EVENT_MAX_TRIGGER_LEN = 15;
constexpr size_t WATERING_EVENT_MAX_RESULT_LEN = 15;

struct WateringEventRecord {
    char requestId[WATERING_EVENT_MAX_REQUEST_ID_LEN + 1];
    char trigger[WATERING_EVENT_MAX_TRIGGER_LEN + 1];
    char result[WATERING_EVENT_MAX_RESULT_LEN + 1];
    uint32_t requestedDurationSec;
    uint32_t actualDurationSec;
    float moistureBeforePct;
    uint32_t timestampSec;
};

// Plain aggregate, same RTC-memory-safety rationale as ReadingBuffer (see
// reading_buffer.h): no constructor, so it's safe to declare RTC_DATA_ATTR.
struct WateringEventBuffer {
    WateringEventRecord events[WATERING_EVENT_BUFFER_CAPACITY];
    size_t count;
};

// Appends a record. If the buffer is full, the oldest entry is dropped to
// make room. MAX_WATERINGS_PER_DAY (config.h) is 4, so this fills only if
// several sync cycles are missed in a row.
void wateringEventBufferPush(WateringEventBuffer& buffer, const WateringEventRecord& record);

void wateringEventBufferClear(WateringEventBuffer& buffer);
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_watering_event_buffer`
Expected: FAIL/build error.

- [ ] **Step 4: Implement**

Create `firmware/src/watering_event_buffer.cpp`:

```cpp
#include "watering_event_buffer.h"

void wateringEventBufferPush(WateringEventBuffer& buffer, const WateringEventRecord& record) {
    if (buffer.count < WATERING_EVENT_BUFFER_CAPACITY) {
        buffer.events[buffer.count++] = record;
        return;
    }
    for (size_t i = 1; i < WATERING_EVENT_BUFFER_CAPACITY; ++i) {
        buffer.events[i - 1] = buffer.events[i];
    }
    buffer.events[WATERING_EVENT_BUFFER_CAPACITY - 1] = record;
}

void wateringEventBufferClear(WateringEventBuffer& buffer) {
    buffer.count = 0;
}
```

- [ ] **Step 5: Add to the native test build filter**

In `firmware/platformio.ini`, under `[env:native]`'s `build_src_filter`, add:

```ini
    +<watering_event_buffer.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_watering_event_buffer`
Expected: PASS, 3 tests.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/watering_event_buffer.h firmware/src/watering_event_buffer.cpp firmware/test/test_watering_event_buffer firmware/platformio.ini
git commit -m "firmware: add RTC-safe WateringEventBuffer for batched cloud/MQTT sync"
```

---

### Task 4: `SyncScheduler` — decide when a wake also does a cloud/MQTT sync

**Files:**
- Create: `firmware/src/sync_scheduler.h`
- Create: `firmware/src/sync_scheduler.cpp`
- Test: `firmware/test/test_sync_scheduler/test_main.cpp`
- Modify: `firmware/platformio.ini` (add `+<sync_scheduler.cpp>`)

**Interfaces:**
- Produces: `bool syncDueThisWake(uint32_t& checksSinceLastSync, uint32_t checksPerSync)`.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_sync_scheduler/test_main.cpp`:

```cpp
#include <unity.h>
#include "sync_scheduler.h"

void setUp(void) {}
void tearDown(void) {}

void test_not_due_before_threshold(void) {
    uint32_t counter = 0;
    TEST_ASSERT_FALSE(syncDueThisWake(counter, 3));
    TEST_ASSERT_EQUAL_UINT32(1, counter);
    TEST_ASSERT_FALSE(syncDueThisWake(counter, 3));
    TEST_ASSERT_EQUAL_UINT32(2, counter);
}

void test_due_at_threshold_and_resets(void) {
    uint32_t counter = 0;
    syncDueThisWake(counter, 3);
    syncDueThisWake(counter, 3);
    TEST_ASSERT_TRUE(syncDueThisWake(counter, 3));
    TEST_ASSERT_EQUAL_UINT32(0, counter);
}

void test_cycle_repeats(void) {
    uint32_t counter = 0;
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_FALSE(syncDueThisWake(counter, 3));
        TEST_ASSERT_FALSE(syncDueThisWake(counter, 3));
        TEST_ASSERT_TRUE(syncDueThisWake(counter, 3));
    }
}

void test_threshold_of_one_syncs_every_wake(void) {
    uint32_t counter = 0;
    TEST_ASSERT_TRUE(syncDueThisWake(counter, 1));
    TEST_ASSERT_TRUE(syncDueThisWake(counter, 1));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_not_due_before_threshold);
    RUN_TEST(test_due_at_threshold_and_resets);
    RUN_TEST(test_cycle_repeats);
    RUN_TEST(test_threshold_of_one_syncs_every_wake);
    return UNITY_END();
}
```

- [ ] **Step 2: Create the header**

Create `firmware/src/sync_scheduler.h`:

```cpp
#pragma once

#include <cstdint>

// Decides whether the current wake should also perform a cloud/MQTT sync,
// based on a count of check-only wakes since the last sync. The counter
// lives in ESP32 RTC memory (declared in main.cpp) and is passed in by
// reference so this stays a pure, host-testable function -- see
// docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md.
//
// Call once per wake. Increments checksSinceLastSync; if it has reached
// checksPerSync, resets it to 0 and returns true (sync due). Otherwise
// returns false.
bool syncDueThisWake(uint32_t& checksSinceLastSync, uint32_t checksPerSync);
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_sync_scheduler`
Expected: FAIL/build error.

- [ ] **Step 4: Implement**

Create `firmware/src/sync_scheduler.cpp`:

```cpp
#include "sync_scheduler.h"

bool syncDueThisWake(uint32_t& checksSinceLastSync, uint32_t checksPerSync) {
    checksSinceLastSync++;
    if (checksSinceLastSync >= checksPerSync) {
        checksSinceLastSync = 0;
        return true;
    }
    return false;
}
```

- [ ] **Step 5: Add to the native test build filter**

In `firmware/platformio.ini`, under `[env:native]`'s `build_src_filter`, add:

```ini
    +<sync_scheduler.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_sync_scheduler`
Expected: PASS, 4 tests.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/sync_scheduler.h firmware/src/sync_scheduler.cpp firmware/test/test_sync_scheduler firmware/platformio.ini
git commit -m "firmware: add SyncScheduler to decide check-only vs. cloud-sync wakes"
```

---

### Task 5: Persist `WateringController` hysteresis/cooldown state across wakes

**Why:** `WateringController` currently keeps `armed_`/`hasWateredBefore_`/`lastWateringEndTime_` as ordinary member state, correct for a continuously-running `loop()`. Once `setup()` reruns on every deep-sleep wake (Task 9), a freshly-constructed `WateringController` would silently reset hysteresis and cooldown on every single wake — defeating both safety mechanisms. This task adds an explicit state snapshot/restore so `main.cpp` can persist just that state in RTC memory.

**Files:**
- Modify: `firmware/src/watering_controller.h` (add nested `State` struct + `getState()`/`restoreState()`)
- Modify: `firmware/src/watering_controller.cpp` (implement them)
- Modify: `firmware/test/test_watering_controller/test_main.cpp` (add coverage)

**Interfaces:**
- Consumes: existing `WateringController` class (`firmware/src/watering_controller.h`), unchanged constructor/`configure()`/`evaluate()`/`notifyWateringComplete()`.
- Produces: `struct WateringController::State { bool armed; bool hasWateredBefore; uint32_t lastWateringEndTime; }`; `WateringController::State WateringController::getState() const`; `void WateringController::restoreState(const State&)`.

- [ ] **Step 1: Write the failing test**

Append to `firmware/test/test_watering_controller/test_main.cpp` (add these functions before `int main`, and add their `RUN_TEST` lines):

```cpp
void test_state_round_trips_through_get_and_restore(void) {
    WateringController controller(30.0f, 5.0f, 900);
    controller.evaluate(20.0f, 1000);
    controller.notifyWateringComplete(1000);

    WateringController::State saved = controller.getState();

    // A freshly-constructed controller (simulating a new wake after deep
    // sleep) starts armed with no watering history.
    WateringController fresh(30.0f, 5.0f, 900);
    WaterDecision beforeRestore = fresh.evaluate(29.0f, 1000 + 1);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED),
                       static_cast<int>(beforeRestore));

    // After restoring the saved state, hysteresis lockout applies exactly
    // as it would have on the original controller.
    WateringController restored(30.0f, 5.0f, 900);
    restored.restoreState(saved);
    WaterDecision afterRestore = restored.evaluate(29.0f, 1000 + 1);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_HYSTERESIS_LOCKOUT),
                       static_cast<int>(afterRestore));
}

void test_state_preserves_cooldown(void) {
    WateringController controller(30.0f, 5.0f, 900);
    controller.evaluate(20.0f, 1000);
    controller.notifyWateringComplete(1000);
    WateringController::State saved = controller.getState();

    WateringController restored(30.0f, 5.0f, 900);
    restored.restoreState(saved);
    // Moisture recovered past rearm, then dried out again inside cooldown.
    restored.evaluate(40.0f, 1000 + 10);
    WaterDecision withinCooldown = restored.evaluate(20.0f, 1200);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_COOLDOWN),
                       static_cast<int>(withinCooldown));
}
```

And add to `main()`:

```cpp
    RUN_TEST(test_state_round_trips_through_get_and_restore);
    RUN_TEST(test_state_preserves_cooldown);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_watering_controller`
Expected: FAIL/build error — `State`/`getState`/`restoreState` undefined.

- [ ] **Step 3: Add the State struct and methods to the header**

In `firmware/src/watering_controller.h`, add to the `public:` section (after `notifyWateringComplete`'s declaration):

```cpp
    // Snapshot of mutable hysteresis/cooldown state, for callers that need
    // to persist it across a reset that reconstructs the object (e.g. an
    // ESP32 deep-sleep wake, which reruns setup() and global constructors
    // -- see main.cpp). A plain aggregate so it's safe to place in RTC
    // memory.
    struct State {
        bool armed;
        bool hasWateredBefore;
        uint32_t lastWateringEndTime;
    };
    State getState() const;
    void restoreState(const State& state);
```

- [ ] **Step 4: Implement in the .cpp**

Append to `firmware/src/watering_controller.cpp`:

```cpp
WateringController::State WateringController::getState() const {
    return State{armed_, hasWateredBefore_, lastWateringEndTime_};
}

void WateringController::restoreState(const State& state) {
    armed_ = state.armed;
    hasWateredBefore_ = state.hasWateredBefore;
    lastWateringEndTime_ = state.lastWateringEndTime;
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_watering_controller`
Expected: PASS, 5 tests (3 existing + 2 new).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/watering_controller.h firmware/src/watering_controller.cpp firmware/test/test_watering_controller/test_main.cpp
git commit -m "firmware: add WateringController state snapshot/restore for deep-sleep persistence"
```

---

### Task 6: Cadence constants and cloud secrets

**Files:**
- Modify: `firmware/include/config.h`
- Modify: `firmware/include/secrets.h.example`

**Interfaces:**
- Produces: `constexpr uint32_t CHECK_INTERVAL_SEC`, `CLOUD_SYNC_INTERVAL_SEC`, `CHECKS_PER_SYNC` (config.h); `constexpr char CLOUD_INGEST_URL[]`, `CLOUD_SHARED_SECRET[]` (secrets.h.example, and each developer's own gitignored `secrets.h`).

- [ ] **Step 1: Add cadence constants to `config.h`**

In `firmware/include/config.h`, insert after the `TIMEZONE_POSIX` constant and before the `// SPEC.md §3 Conventions` comment:

```cpp
// Standalone deep-sleep cadence -- see
// docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md.
// The check/water loop runs every wake (no radio); the cloud/MQTT sync
// loop only runs every CHECKS_PER_SYNC-th wake.
constexpr uint32_t CHECK_INTERVAL_SEC = 600;         // 10 min
constexpr uint32_t CLOUD_SYNC_INTERVAL_SEC = 18000;  // 5 hours
constexpr uint32_t CHECKS_PER_SYNC = CLOUD_SYNC_INTERVAL_SEC / CHECK_INTERVAL_SEC;
```

- [ ] **Step 2: Add cloud secrets to `secrets.h.example`**

In `firmware/include/secrets.h.example`, append after the `OTA_PASSWORD` line:

```cpp

// Cloudflare Worker ingest endpoint (standalone deployment, SPEC.md §5).
// Ask whoever deployed the Worker (see the cloudflare-ingest-service plan)
// for the real URL and shared secret.
constexpr char CLOUD_INGEST_URL[] = "https://your-worker.your-subdomain.workers.dev/ingest";
constexpr char CLOUD_SHARED_SECRET[] = "your_shared_secret";
```

- [ ] **Step 3: Update your local `secrets.h`**

Copy the same two lines into your gitignored `firmware/include/secrets.h` (create it from the example first if you haven't: `cp firmware/include/secrets.h.example firmware/include/secrets.h`). Use a placeholder URL/secret for now if the Cloudflare Worker from the companion plan isn't deployed yet — Task 9's `sendIngestPayload()` call will simply fail gracefully and retry next sync.

- [ ] **Step 4: Verify the native suite still builds**

Run: `cd firmware && python -m platformio test -e native`
Expected: all existing tests still PASS (config.h changes are additive; `secrets.h` already existed).

- [ ] **Step 5: Commit**

```bash
git add firmware/include/config.h firmware/include/secrets.h.example
git commit -m "firmware: add deep-sleep cadence constants and cloud ingest secrets"
```

(`secrets.h` itself is gitignored — nothing to commit there.)

---

### Task 7: `buildIngestPayload` — pure JSON payload builder

**Files:**
- Create: `firmware/src/cloud_payload.h`
- Create: `firmware/src/cloud_payload.cpp`
- Test: `firmware/test/test_cloud_payload/test_main.cpp`
- Modify: `firmware/platformio.ini` (add `+<cloud_payload.cpp>`)

**Interfaces:**
- Consumes: `ReadingBuffer` (Task 2), `WateringEventBuffer`/`WateringEventRecord` (Task 3).
- Produces: `constexpr size_t CLOUD_PAYLOAD_MAX_LEN`; `size_t buildIngestPayload(char* outBuffer, size_t outBufferSize, const char* deviceId, const ReadingBuffer& readings, const WateringEventBuffer& events, int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec, uint32_t timestamp)`.

- [ ] **Step 1: Write the failing test**

Create `firmware/test/test_cloud_payload/test_main.cpp`:

```cpp
#include <cstring>
#include <unity.h>
#include "cloud_payload.h"
#include "reading_buffer.h"
#include "watering_event_buffer.h"

void setUp(void) {}
void tearDown(void) {}

void test_empty_buffers_produce_valid_shape(void) {
    ReadingBuffer readings = {};
    WateringEventBuffer events = {};
    char buf[CLOUD_PAYLOAD_MAX_LEN];

    size_t len = buildIngestPayload(buf, sizeof(buf), "zone_1", readings, events,
                                     -62, 3.98f, 86412, 1753277970);

    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device_id\":\"zone_1\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"readings\":[]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"watering_events\":[]"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device_status\":{\"wifi_rssi_dbm\":-62"));
}

void test_readings_and_events_are_included(void) {
    ReadingBuffer readings = {};
    readingBufferPush(readings, 42.5f, 1753277940);

    WateringEventBuffer events = {};
    WateringEventRecord record = {};
    strncpy(record.requestId, "local_7", sizeof(record.requestId) - 1);
    strncpy(record.trigger, "moisture", sizeof(record.trigger) - 1);
    strncpy(record.result, "completed", sizeof(record.result) - 1);
    record.requestedDurationSec = 10;
    record.actualDurationSec = 10;
    record.moistureBeforePct = 28.0f;
    record.timestampSec = 1753277961;
    wateringEventBufferPush(events, record);

    char buf[CLOUD_PAYLOAD_MAX_LEN];
    size_t len = buildIngestPayload(buf, sizeof(buf), "zone_1", readings, events,
                                     -62, 3.98f, 86412, 1753277970);

    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"moisture_pct\":42.5"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"request_id\":\"local_7\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"result\":\"completed\""));
}

void test_undersized_buffer_returns_zero(void) {
    ReadingBuffer readings = {};
    readingBufferPush(readings, 42.5f, 1753277940);
    WateringEventBuffer events = {};

    char tiny[8];
    size_t len = buildIngestPayload(tiny, sizeof(tiny), "zone_1", readings, events,
                                     -62, 3.98f, 86412, 1753277970);
    TEST_ASSERT_EQUAL(0, static_cast<int>(len));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_empty_buffers_produce_valid_shape);
    RUN_TEST(test_readings_and_events_are_included);
    RUN_TEST(test_undersized_buffer_returns_zero);
    return UNITY_END();
}
```

- [ ] **Step 2: Create the header**

Create `firmware/src/cloud_payload.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "reading_buffer.h"
#include "watering_event_buffer.h"

// Builds the JSON body POSTed to the Cloudflare Worker's /ingest endpoint --
// see SPEC.md §5. Pure string building (snprintf), no network/Arduino
// dependency, so it's testable under the native target the same way
// mqtt_client.cpp's payload builders would be if that file weren't bundled
// with PubSubClient/Arduino.
//
// Returns the number of bytes written (excluding the null terminator), or
// 0 if outBufferSize was too small to fit the full payload (nothing
// partial is left in outBuffer in that case). Callers should size
// outBuffer as CLOUD_PAYLOAD_MAX_LEN.
constexpr size_t CLOUD_PAYLOAD_MAX_LEN = 2048;

size_t buildIngestPayload(char* outBuffer, size_t outBufferSize,
                           const char* deviceId,
                           const ReadingBuffer& readings,
                           const WateringEventBuffer& events,
                           int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec,
                           uint32_t timestamp);
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd firmware && python -m platformio test -e native -f test_cloud_payload`
Expected: FAIL/build error.

- [ ] **Step 4: Implement**

Create `firmware/src/cloud_payload.cpp`:

```cpp
#include "cloud_payload.h"

#include <cstdarg>
#include <cstdio>

namespace {
bool appendf(char* buffer, size_t bufferSize, size_t& offset, const char* fmt, ...) {
    if (offset >= bufferSize) {
        return false;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + offset, bufferSize - offset, fmt, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= bufferSize - offset) {
        return false; // truncated or encoding error
    }
    offset += static_cast<size_t>(written);
    return true;
}
}

size_t buildIngestPayload(char* outBuffer, size_t outBufferSize,
                           const char* deviceId,
                           const ReadingBuffer& readings,
                           const WateringEventBuffer& events,
                           int wifiRssiDbm, float batteryVoltageV, uint32_t uptimeSec,
                           uint32_t timestamp) {
    size_t offset = 0;

    if (!appendf(outBuffer, outBufferSize, offset,
                 "{\"device_id\":\"%s\",\"timestamp\":%lu,\"readings\":[",
                 deviceId, static_cast<unsigned long>(timestamp))) {
        return 0;
    }

    for (size_t i = 0; i < readings.count; ++i) {
        if (!appendf(outBuffer, outBufferSize, offset,
                     "%s{\"moisture_pct\":%.1f,\"timestamp\":%lu}",
                     i == 0 ? "" : ",", readings.moisturePct[i],
                     static_cast<unsigned long>(readings.timestampSec[i]))) {
            return 0;
        }
    }

    if (!appendf(outBuffer, outBufferSize, offset, "],\"watering_events\":[")) {
        return 0;
    }

    for (size_t i = 0; i < events.count; ++i) {
        const WateringEventRecord& e = events.events[i];
        if (!appendf(outBuffer, outBufferSize, offset,
                     "%s{\"request_id\":\"%s\",\"trigger\":\"%s\",\"result\":\"%s\","
                     "\"requested_duration_sec\":%lu,\"actual_duration_sec\":%lu,"
                     "\"moisture_before_pct\":%.1f,\"timestamp\":%lu}",
                     i == 0 ? "" : ",", e.requestId, e.trigger, e.result,
                     static_cast<unsigned long>(e.requestedDurationSec),
                     static_cast<unsigned long>(e.actualDurationSec),
                     e.moistureBeforePct, static_cast<unsigned long>(e.timestampSec))) {
            return 0;
        }
    }

    if (!appendf(outBuffer, outBufferSize, offset,
                 "],\"device_status\":{\"wifi_rssi_dbm\":%d,\"battery_voltage_v\":%.2f,"
                 "\"uptime_sec\":%lu}}",
                 wifiRssiDbm, batteryVoltageV, static_cast<unsigned long>(uptimeSec))) {
        return 0;
    }

    return offset;
}
```

- [ ] **Step 5: Add to the native test build filter**

In `firmware/platformio.ini`, under `[env:native]`'s `build_src_filter`, add:

```ini
    +<cloud_payload.cpp>
```

- [ ] **Step 6: Run test to verify it passes**

Run: `cd firmware && python -m platformio test -e native -f test_cloud_payload`
Expected: PASS, 3 tests.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/cloud_payload.h firmware/src/cloud_payload.cpp firmware/test/test_cloud_payload firmware/platformio.ini
git commit -m "firmware: add buildIngestPayload for SPEC.md §5 cloud ingest JSON"
```

---

### Task 8: `sendIngestPayload` — hardware-only HTTP POST

**Files:**
- Create: `firmware/src/cloud_client.h`
- Create: `firmware/src/cloud_client.cpp`

**Interfaces:**
- Consumes: `CLOUD_INGEST_URL`, `CLOUD_SHARED_SECRET` (Task 6, `secrets.h`).
- Produces: `bool sendIngestPayload(const char* payload)`.

Not host-testable — requires `HTTPClient`/`WiFiClientSecure`, real hardware, and a reachable Worker, same limitation `mqtt_client.cpp` already documents for MQTT. Only builds under `esp32dev`; not added to `env:native`'s `build_src_filter`.

- [ ] **Step 1: Create the header**

Create `firmware/src/cloud_client.h`:

```cpp
#pragma once

// Sends a pre-built /ingest payload (see cloud_payload.h) to the Cloudflare
// Worker over HTTPS. REQUIRES REAL HARDWARE (WiFi/HTTPClient) -- only
// builds under the esp32dev environment, same limitation as
// mqtt_client.cpp. Not exercised by `pio test -e native`.
bool sendIngestPayload(const char* payload);
```

- [ ] **Step 2: Implement**

Create `firmware/src/cloud_client.cpp`:

```cpp
#include "cloud_client.h"
#include "config.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstdio>
#include <cstring>

bool sendIngestPayload(const char* payload) {
    WiFiClientSecure client;
    // Matches the MQTT_USE_TLS / setInsecure() precedent already documented
    // in secrets.h.example: fine for local/dev, replace with a pinned CA
    // once the Worker's cert chain is known to be stable.
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, CLOUD_INGEST_URL)) {
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    char authHeader[96];
    snprintf(authHeader, sizeof(authHeader), "Bearer %s", CLOUD_SHARED_SECRET);
    http.addHeader("Authorization", authHeader);

    int statusCode = http.POST(reinterpret_cast<const uint8_t*>(payload), strlen(payload));
    http.end();
    return statusCode >= 200 && statusCode < 300;
}
```

- [ ] **Step 3: Compile-check under the esp32dev target**

Run: `cd firmware && python -m platformio run -e esp32dev`
Expected: builds successfully (compile-only check, no board attached — same as the existing `mqtt_client.cpp` verification story).

- [ ] **Step 4: Commit**

```bash
git add firmware/src/cloud_client.h firmware/src/cloud_client.cpp
git commit -m "firmware: add sendIngestPayload HTTP client for cloud sync"
```

---

### Task 9: Restructure `main.cpp` into the deep-sleep cycle

**Files:**
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: everything from Tasks 2-8 (`ReadingBuffer`/`readingBufferPush`/`readingBufferClear`, `WateringEventBuffer`/`WateringEventRecord`/`wateringEventBufferPush`/`wateringEventBufferClear`, `syncDueThisWake`, `WateringController::State`/`getState`/`restoreState`, `CHECK_INTERVAL_SEC`/`CLOUD_SYNC_INTERVAL_SEC`/`CHECKS_PER_SYNC`, `buildIngestPayload`/`CLOUD_PAYLOAD_MAX_LEN`, `sendIngestPayload`) plus existing modules unchanged (`hal/moisture_sensor.h`, `hal/pump_relay.h` via `pump_runner.h`, `hal/clock.h`, `watering_controller.h`, `schedule.h`, `wifi_manager.h`, `mqtt_client.h`, `device_status.h`, `watchdog.h`, `pump_runner.h`, `request_id_history.h`, `config_store.h`, `ota_handler.h`, `sleep_manager.h`).
- Produces: nothing further consumed elsewhere — this is the final integration point.

Not host-testable (this file is `esp32dev`-only, same as before). Verified by compiling the real target.

- [ ] **Step 1: Replace the contents of `main.cpp`**

Replace the entire contents of `firmware/src/main.cpp` with:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

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
#include "pump_runner.h"
#include "request_id_history.h"
#include "config_store.h"
#include "ota_handler.h"
#include "sleep_manager.h"
#include "reading_buffer.h"
#include "watering_event_buffer.h"
#include "sync_scheduler.h"
#include "cloud_payload.h"
#include "cloud_client.h"

namespace {
WifiManager g_wifiManager(1000, 30000);

Client& networkClient() {
    if (MQTT_USE_TLS) {
        static WiFiClientSecure instance;
        if (MQTT_BROKER_CA_CERT[0] != '\0') {
            instance.setCACert(MQTT_BROKER_CA_CERT);
        } else {
            instance.setInsecure();
        }
        return instance;
    }
    static WiFiClient instance;
    return instance;
}

MqttClient g_mqtt(networkClient(), MQTT_BROKER_HOST, MQTT_BROKER_PORT);
ConfigStore g_configStore;
WateringController g_wateringController(
    g_configStore.moistureThresholdPct(),
    g_configStore.moistureHysteresisPct(),
    g_configStore.cooldownPeriodSec());
Schedule g_schedule(ScheduleConfig{
    g_configStore.scheduleWindowStartHour(),
    g_configStore.scheduleWindowEndHour(),
    g_configStore.maxWateringsPerDay()});
PumpRunner g_pumpRunner;
RequestIdHistory g_commandRequestIdHistory;

// Survives deep sleep: ESP-IDF skips re-initializing RTC memory on a
// deep-sleep timer wake. Plain aggregates only -- see reading_buffer.h /
// watering_event_buffer.h / watering_controller.h for why a type with a
// non-trivial constructor would be unsafe here.
RTC_DATA_ATTR uint32_t g_checksSinceLastSync = 0;
RTC_DATA_ATTR ReadingBuffer g_readingBuffer = {};
RTC_DATA_ATTR WateringEventBuffer g_wateringEventBuffer = {};
RTC_DATA_ATTR WateringController::State g_wateringControllerState = {true, false, 0};
RTC_DATA_ATTR int g_wateringsToday = 0;
RTC_DATA_ATTR int g_lastCountedDay = -1;

uint32_t g_nextLocalRequestId = 1;

void makeLocalRequestId(char* buffer, size_t size) {
    snprintf(buffer, size, "local_%lu", static_cast<unsigned long>(g_nextLocalRequestId++));
}

float clampMoisture(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

void onPumpCommand(char* topic, uint8_t* payload, unsigned int length) {
    (void)topic;

    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err != DeserializationError::Ok) {
        return;
    }

    const char* deviceId = doc["device_id"];
    if (!deviceId || strcmp(deviceId, DEVICE_ID) != 0) {
        return;
    }

    const char* command = doc["command"];
    if (!command) {
        return;
    }

    const char* providedRequestId = doc["request_id"];
    char localRequestIdBuffer[32];
    if (!providedRequestId || providedRequestId[0] == '\0') {
        makeLocalRequestId(localRequestIdBuffer, sizeof(localRequestIdBuffer));
        providedRequestId = localRequestIdBuffer;
    }
    const char* requestId = providedRequestId;
    const uint32_t nowSec = currentUnixTimeSec();

    if (g_mqtt.isConnected()) {
        g_mqtt.publishPumpAck(requestId, nowSec);
    }

    if (g_commandRequestIdHistory.isDuplicate(requestId)) {
        return;
    }

    // Manual "run"/"stop" commands only have a chance to arrive during the
    // brief cloud-sync window (MQTT is only connected then) -- see
    // docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md. A
    // command sent while the device is asleep is simply missed, same as
    // any MQTT message with no persistent session; this is a known,
    // accepted limitation of running standalone on battery.
    if (strcmp(command, "run") == 0) {
        const uint32_t minDurationSec = g_configStore.minWateringDurationSec();
        uint32_t durationSec = doc["duration_sec"] | minDurationSec;
        if (durationSec < minDurationSec) {
            durationSec = minDurationSec;
        }

        const char* trigger = doc["trigger"] | "manual";

        float moistureBeforePct = clampMoisture(readMoisturePercent());
        if (g_pumpRunner.isRunning()) {
            return;
        }

        g_pumpRunner.start(millis(), nowSec, durationSec, requestId, trigger, moistureBeforePct);
    } else if (strcmp(command, "stop") == 0) {
        PumpRunner::Result result;
        g_pumpRunner.stop(millis(), nowSec, result);
    }
}

// Blocks (feeding the watchdog) until the current pump run finishes, or a
// safety-margin timeout forces it off. Deep sleep would cut power mid-run,
// so a triggered watering must complete before setup() returns.
void runPumpToCompletion() {
    const uint32_t maxWaitMs = (g_configStore.minWateringDurationSec() + 10) * 1000;
    uint32_t startWaitMs = millis();
    PumpRunner::Result result;
    PumpRunResult runResult = PumpRunResult::NONE;

    while (g_pumpRunner.isRunning() && (millis() - startWaitMs) < maxWaitMs) {
        feedWatchdog();
        runResult = g_pumpRunner.update(millis(), currentUnixTimeSec(), result);
        if (runResult == PumpRunResult::COMPLETED) {
            break;
        }
        delay(50);
    }

    if (g_pumpRunner.isRunning()) {
        // Safety fallback: force off rather than let deep sleep leave the
        // relay state undefined.
        runResult = g_pumpRunner.stop(millis(), currentUnixTimeSec(), result);
    }

    if (runResult == PumpRunResult::COMPLETED || runResult == PumpRunResult::STOPPED_EARLY) {
        g_wateringController.notifyWateringComplete(result.completedAtSec);
        g_wateringControllerState = g_wateringController.getState();
        g_wateringsToday++;

        WateringEventRecord record = {};
        strncpy(record.requestId, result.requestId, sizeof(record.requestId) - 1);
        record.requestId[sizeof(record.requestId) - 1] = '\0';
        strncpy(record.trigger, result.trigger, sizeof(record.trigger) - 1);
        record.trigger[sizeof(record.trigger) - 1] = '\0';
        // Both COMPLETED and STOPPED_EARLY are reported as "completed" per
        // SPEC.md §1 -- STOPPED_EARLY just means actual_duration_sec <
        // requested_duration_sec.
        strncpy(record.result, "completed", sizeof(record.result) - 1);
        record.result[sizeof(record.result) - 1] = '\0';
        record.requestedDurationSec = result.requestedDurationSec;
        record.actualDurationSec = result.actualDurationSec;
        record.moistureBeforePct = result.moistureBeforePct;
        record.timestampSec = result.completedAtSec;
        wateringEventBufferPush(g_wateringEventBuffer, record);
    }
}

// Brings WiFi up (bounded wait), resyncs time, opportunistically publishes
// to MQTT (dormant Pi hook), and pushes the accumulated buffers to the
// Cloudflare Worker. Buffers are only cleared once the cloud push actually
// succeeds, so a failed sync is retried (up to buffer capacity) next
// cycle.
void performCloudSync() {
    const uint32_t wifiTimeoutMs = 20000;
    uint32_t startMs = millis();
    while (g_wifiManager.state() != WifiState::CONNECTED && (millis() - startMs) < wifiTimeoutMs) {
        feedWatchdog();
        g_wifiManager.update(millis());
        delay(50);
    }

    if (g_wifiManager.state() != WifiState::CONNECTED) {
        return;
    }

    initClockTimezone(TIMEZONE_POSIX); // re-triggers NTP resync; see hal/clock.h

    if (g_mqtt.connect(DEVICE_ID)) {
        g_mqtt.subscribeToPumpCommand(onPumpCommand);
        g_mqtt.publishDeviceStatus(readWifiRssiDbm(), readBatteryVoltageV(), readUptimeSec(), currentUnixTimeSec());
        for (size_t i = 0; i < g_readingBuffer.count; ++i) {
            g_mqtt.publishMoistureReading(g_readingBuffer.moisturePct[i], g_readingBuffer.timestampSec[i]);
        }
        for (size_t i = 0; i < g_wateringEventBuffer.count; ++i) {
            const WateringEventRecord& e = g_wateringEventBuffer.events[i];
            g_mqtt.publishPumpStatus(e.requestId, e.trigger, e.result, e.requestedDurationSec,
                                     e.actualDurationSec, e.moistureBeforePct, e.timestampSec);
        }
        g_mqtt.loop();
    }
    // MQTT is a dormant, opportunistic hook for a future Pi -- its failure
    // must never block the cloud push below.

    char payload[CLOUD_PAYLOAD_MAX_LEN];
    size_t len = buildIngestPayload(payload, sizeof(payload), DEVICE_ID,
                                     g_readingBuffer, g_wateringEventBuffer,
                                     readWifiRssiDbm(), readBatteryVoltageV(), readUptimeSec(),
                                     currentUnixTimeSec());
    if (len > 0 && sendIngestPayload(payload)) {
        readingBufferClear(g_readingBuffer);
        wateringEventBufferClear(g_wateringEventBuffer);
    }
}
}

void setup() {
    Serial.begin(115200);
    enforceSafeDefaultPumpOff();
    setupWatchdog(30);
    initClockTimezone(TIMEZONE_POSIX);
    setupOta(OTA_PASSWORD);

    g_configStore.load();
    g_wateringController.configure(
        g_configStore.moistureThresholdPct(),
        g_configStore.moistureHysteresisPct(),
        g_configStore.cooldownPeriodSec());
    g_wateringController.restoreState(g_wateringControllerState);
    g_schedule.configure(ScheduleConfig{
        g_configStore.scheduleWindowStartHour(),
        g_configStore.scheduleWindowEndHour(),
        g_configStore.maxWateringsPerDay()});

    const uint32_t nowSec = currentUnixTimeSec();
    const int today = static_cast<int>(nowSec / 86400);
    if (today != g_lastCountedDay) {
        g_wateringsToday = 0;
        g_lastCountedDay = today;
    }

    // 1. Check/water -- every wake, no radio.
    float moisturePct = clampMoisture(readMoisturePercent());
    readingBufferPush(g_readingBuffer, moisturePct, nowSec);

    WaterDecision decision = g_wateringController.evaluate(moisturePct, nowSec);
    if (decision == WaterDecision::WATER_TRIGGERED &&
        g_schedule.allowsWatering(currentLocalHour(), g_wateringsToday) &&
        !g_pumpRunner.isRunning()) {
        char requestId[32];
        makeLocalRequestId(requestId, sizeof(requestId));
        g_pumpRunner.start(millis(), nowSec, g_configStore.minWateringDurationSec(),
                           requestId, "moisture", moisturePct);
        runPumpToCompletion();
    }

    // 2. Cloud/MQTT sync -- only every CHECKS_PER_SYNC-th wake.
    if (syncDueThisWake(g_checksSinceLastSync, CHECKS_PER_SYNC)) {
        performCloudSync();
    }

    // 3. Sleep until the next check.
    enterDeepSleep(CHECK_INTERVAL_SEC);
}

void loop() {
    // Never reached: enterDeepSleep() resets the device before returning here.
}
```

- [ ] **Step 2: Update `sleep_manager.h`'s header comment**

`firmware/src/sleep_manager.h` currently says it's "intentionally not called from main.cpp's loop() yet." Replace that sentence (the paragraph starting "This function is correct per the ESP32 Arduino core's documented API, but untested here") with:

```cpp
// This function is correct per the ESP32 Arduino core's documented API,
// but its actual current-draw and wake-timing behavior are UNVERIFIED --
// see the "real-hardware verification items" in
// docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md. It is now
// wired into main.cpp's setup() (called at the end of every wake cycle).
```

- [ ] **Step 3: Compile-check under the esp32dev target**

Run: `cd firmware && python -m platformio run -e esp32dev`
Expected: builds successfully.

- [ ] **Step 4: Run the full native suite to confirm nothing else broke**

Run: `cd firmware && python -m platformio test -e native`
Expected: all tests PASS (main.cpp isn't part of the native build; this just confirms Tasks 2-7's additions didn't regress anything else).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/main.cpp firmware/src/sleep_manager.h
git commit -m "firmware: restructure main.cpp into a deep-sleep check/water + cloud-sync cycle"
```

---

### Task 10: Update docs

**Files:**
- Modify: `firmware/README.md`
- Modify: `README.md` (repo root)

**Interfaces:** None (documentation only).

- [ ] **Step 1: Update `firmware/README.md`**

Replace the `## Remote-prep phase (no hardware yet)` section's closing paragraph (the one starting "`sleep_manager.cpp`'s `enterDeepSleep()`...") with:

```markdown
## Standalone deep-sleep cycle

The device does not run a continuous `loop()`. `setup()` runs the entire cycle once per
wake and ends by calling `enterDeepSleep()`, which resets the device — `loop()` is never
reached. Every wake: read moisture, run the existing decision logic, water if needed.
Every `CHECKS_PER_SYNC`-th wake (roughly every `CLOUD_SYNC_INTERVAL_SEC`, both in
`config.h`): also connect WiFi, resync time, publish to MQTT (dormant, for a future Pi —
see `SPEC.md` §1), and POST buffered readings/events to the Cloudflare Worker (`SPEC.md`
§5). See `docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md` for the full
rationale.

`sleep_manager.cpp`'s `enterDeepSleep()` is wired into `main.cpp` now, but its actual
current-draw and wake-timing behavior are **unverified on real silicon** — flagged as a
real-hardware verification item, not a blocking concern for the software itself.
```

- [ ] **Step 2: Update the hardware swap point table**

In `firmware/README.md`'s existing table, the `readBatteryVoltageV()` row's "Real implementation" cell already says "real ADC read + voltage divider math" — leave it as-is (this plan doesn't implement it; the exact divider ratio depends on the battery/enclosure wiring chosen at ordering time). Add one row:

```markdown
| `sendIngestPayload()` | `src/cloud_client.cpp` | Already real (`HTTPClient`) — needs `CLOUD_INGEST_URL`/`CLOUD_SHARED_SECRET` in `secrets.h` pointed at a deployed Worker (see the cloudflare-ingest-service plan) |
```

- [ ] **Step 3: Update the root `README.md`**

In the `## Architecture` section's diagram and prose, and the `## When hardware arrives` section, add a short note (don't rewrite the whole file — this plan only changes what's now inaccurate):

After the existing architecture diagram, add:

```markdown
**Standalone deployment note:** the diagram above is the eventual two-device shape. The
current build ships the ESP32 half standalone — deep-sleep cycle, battery-powered,
reporting to a Cloudflare Worker instead of (or alongside) a Pi. See
`docs/superpowers/specs/2026-08-07-standalone-outdoor-design.md` and
`docs/superpowers/plans/2026-08-07-cloudflare-ingest-service.md`.
```

- [ ] **Step 4: Commit**

```bash
git add firmware/README.md README.md
git commit -m "docs: document the standalone deep-sleep cycle and cloud ingest endpoint"
```

---

## Self-Review Notes

- **Spec coverage:** Task 1 covers the SPEC.md contract; Tasks 2-4 cover the design's "decoupled loops" (reading buffer, event buffer, sync scheduler); Task 5 covers the hysteresis/cooldown persistence bug this restructure would otherwise introduce; Task 6 covers cadence constants + secrets; Tasks 7-8 cover the cloud payload/client split (mirroring the existing pure/hardware split used elsewhere in this codebase); Task 9 covers wiring `sleep_manager.cpp` in, per the design's explicit goal; Task 10 covers docs. Battery-voltage-becomes-real and the exact BOM (moisture sensor model, pump voltage, relay/MOSFET choice) are explicitly out of scope per the design doc (decided at ordering time) and are not tasks here.
- **Type consistency:** `WateringEventRecord` field sizes (`requestId[37]`, `trigger[16]`) match `PumpRunner::Result`'s `MAX_REQUEST_ID_LEN`(36)+1 / `MAX_TRIGGER_LEN`(15)+1 exactly, since Task 9 copies directly from one into the other. `buildIngestPayload`'s signature in Task 7 matches exactly how Task 9 calls it.
- **RTC-safety pattern:** applied consistently across Tasks 2, 3, 5, and 9 (`ReadingBuffer`, `WateringEventBuffer`, `WateringController::State`, plus the plain `uint32_t`/`int` counters) — every RTC_DATA_ATTR global is a plain aggregate or primitive, none has a non-trivial constructor.
