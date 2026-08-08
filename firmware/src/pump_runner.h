#pragma once

#include <cstddef>
#include <cstdint>

// Result codes returned by PumpRunner::update() and PumpRunner::stop().
enum class PumpRunResult {
    NONE,           // No state change this call.
    COMPLETED,      // Requested duration elapsed; pump was turned off.
    STOPPED_EARLY,  // stop() was called before duration elapsed.
};

// Non-blocking pump state machine.
//
// Call start() to energize the relay and begin tracking elapsed time. Call
// update() once per loop; it returns COMPLETED when the requested duration has
// elapsed. Call stop() to turn the pump off early (for example, in response to
// a MQTT "stop" command). The relay is always turned off through setPumpRelay()
// so the real-hardware swap point remains in hal/pump_relay.cpp.
//
// Timestamps are injected by the caller so this class has no Arduino
// dependency and remains testable under PlatformIO's `native` environment.
class PumpRunner {
public:
    static constexpr size_t MAX_REQUEST_ID_LEN = 36;
    static constexpr size_t MAX_TRIGGER_LEN = 15;

    struct Result {
        char requestId[MAX_REQUEST_ID_LEN + 1];
        char trigger[MAX_TRIGGER_LEN + 1];
        uint32_t requestedDurationSec;
        uint32_t actualDurationSec;
        float moistureBeforePct;
        uint32_t completedAtSec;
    };

    PumpRunner() = default;

    // True if the pump relay is currently energized.
    bool isRunning() const;

    // Starts a run. `nowMs` is the current monotonic millisecond timestamp
    // (e.g. millis()) and `nowSec` is the current Unix epoch second.
    // Returns false if a run is already in progress or if durationSec is zero.
    // Energizes the relay immediately.
    bool start(uint32_t nowMs, uint32_t nowSec, uint32_t durationSec,
               const char* requestId, const char* trigger, float moistureBeforePct);

    // Call once per main loop. Returns COMPLETED and fills outResult when the
    // requested duration has elapsed; otherwise returns NONE.
    PumpRunResult update(uint32_t nowMs, uint32_t nowSec, Result& outResult);

    // Stops an in-progress run early. Fills outResult and returns
    // STOPPED_EARLY, or returns NONE if no run is active.
    PumpRunResult stop(uint32_t nowMs, uint32_t nowSec, Result& outResult);

private:
    void populateResult(Result& outResult, uint32_t actualDurationSec,
                        uint32_t completedAtSec);

    bool running_ = false;
    uint32_t startMs_ = 0;
    uint32_t durationSec_ = 0;
    char requestId_[MAX_REQUEST_ID_LEN + 1] = {};
    char trigger_[MAX_TRIGGER_LEN + 1] = {};
    float moistureBeforePct_ = 0.0f;
};
