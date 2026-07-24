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
