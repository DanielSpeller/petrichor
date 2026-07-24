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
