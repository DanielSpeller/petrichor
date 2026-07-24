#include "sleep_manager.h"
#include <esp_sleep.h>
#include <Arduino.h>

void enterDeepSleep(uint32_t sleepDurationSec) {
    // NEEDS REAL-DEVICE TESTING -- see header comment.
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleepDurationSec) * 1000000ULL);
    esp_deep_sleep_start();
}
