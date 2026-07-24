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
