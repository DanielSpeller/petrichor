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
