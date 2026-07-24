#include "hal/clock.h"
#include <Arduino.h>

namespace {
constexpr uint32_t MOCK_START_UNIX_TIME = 1753277940; // arbitrary fixed epoch for mock runs
}

uint32_t currentUnixTimeSec() {
    // HARDWARE SWAP POINT: replace with real NTP-synced time.
    return MOCK_START_UNIX_TIME + (millis() / 1000);
}

int currentLocalHour() {
    return static_cast<int>((currentUnixTimeSec() / 3600) % 24);
}
