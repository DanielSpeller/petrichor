#pragma once

#include <cstdint>

// Deep sleep wake/sleep cycle scaffold.
//
// NEEDS REAL-DEVICE TESTING: deep sleep behavior (timer wake source,
// current draw, GPIO state retention across sleep) can only be
// meaningfully verified on real ESP32 hardware. main.cpp now calls this
// at the end of every wake cycle.
void enterDeepSleep(uint32_t sleepDurationSec);
