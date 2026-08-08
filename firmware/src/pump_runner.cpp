#include "pump_runner.h"

#include "hal/pump_relay.h"

#include <cstring>

bool PumpRunner::isRunning() const {
    return running_;
}

bool PumpRunner::start(uint32_t nowMs, uint32_t nowSec, uint32_t durationSec,
                       const char* requestId, const char* trigger,
                       float moistureBeforePct) {
    (void)nowSec;

    if (running_ || durationSec == 0) {
        return false;
    }

    running_ = true;
    startMs_ = nowMs;
    durationSec_ = durationSec;
    moistureBeforePct_ = moistureBeforePct;

    strncpy(requestId_, requestId, MAX_REQUEST_ID_LEN);
    requestId_[MAX_REQUEST_ID_LEN] = '\0';
    strncpy(trigger_, trigger, MAX_TRIGGER_LEN);
    trigger_[MAX_TRIGGER_LEN] = '\0';

    setPumpRelay(true);
    return true;
}

PumpRunResult PumpRunner::update(uint32_t nowMs, uint32_t nowSec, Result& outResult) {
    if (!running_) {
        return PumpRunResult::NONE;
    }

    // A single watering run is short (seconds or minutes), so millis()
    // wrapping every ~49 days is not a practical concern here.
    uint32_t elapsedMs = nowMs - startMs_;
    if (elapsedMs >= durationSec_ * 1000UL) {
        setPumpRelay(false);
        running_ = false;
        populateResult(outResult, durationSec_, nowSec);
        return PumpRunResult::COMPLETED;
    }

    return PumpRunResult::NONE;
}

PumpRunResult PumpRunner::stop(uint32_t nowMs, uint32_t nowSec, Result& outResult) {
    if (!running_) {
        return PumpRunResult::NONE;
    }

    setPumpRelay(false);

    uint32_t elapsedSec = (nowMs - startMs_) / 1000UL;
    if (elapsedSec > durationSec_) {
        elapsedSec = durationSec_;
    }

    running_ = false;
    populateResult(outResult, elapsedSec, nowSec);
    return PumpRunResult::STOPPED_EARLY;
}

void PumpRunner::populateResult(Result& outResult, uint32_t actualDurationSec,
                                uint32_t completedAtSec) {
    strncpy(outResult.requestId, requestId_, MAX_REQUEST_ID_LEN);
    outResult.requestId[MAX_REQUEST_ID_LEN] = '\0';
    strncpy(outResult.trigger, trigger_, MAX_TRIGGER_LEN);
    outResult.trigger[MAX_TRIGGER_LEN] = '\0';
    outResult.requestedDurationSec = durationSec_;
    outResult.actualDurationSec = actualDurationSec;
    outResult.moistureBeforePct = moistureBeforePct_;
    outResult.completedAtSec = completedAtSec;
}
