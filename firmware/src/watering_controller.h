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
