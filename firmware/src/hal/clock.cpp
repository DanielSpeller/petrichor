#include "hal/clock.h"

#include <cstdlib>
#include <ctime>

#ifdef _WIN32
// Windows/MinGW native build does not provide POSIX setenv/localtime_r.
#include <stdlib.h>
static int setenv_compat(const char* name, const char* value, int) {
    return _putenv_s(name, value);
}
static struct tm* localtime_compat(const time_t* timep, struct tm* result) {
    if (localtime_s(result, timep) != 0) {
        return nullptr;
    }
    return result;
}
#else
static int setenv_compat(const char* name, const char* value, int overwrite) {
    return setenv(name, value, overwrite);
}
static struct tm* localtime_compat(const time_t* timep, struct tm* result) {
    return localtime_r(timep, result);
}
#endif

#ifdef ARDUINO
#include <Arduino.h>
#else
// Native test shim: millis() is not available, so time stands still at 0.
static uint32_t millis() { return 0; }
#endif

namespace {
constexpr uint32_t MOCK_START_UNIX_TIME = 1753277940; // arbitrary fixed epoch for mock runs
bool g_timezoneInitialized = false;
}

void initClockTimezone(const char* tzPosix) {
    if (!tzPosix || tzPosix[0] == '\0') {
        setenv_compat("TZ", "UTC0", 1);
    } else {
        setenv_compat("TZ", tzPosix, 1);
    }
    tzset();
    g_timezoneInitialized = true;
}

uint32_t currentUnixTimeSec() {
    // HARDWARE SWAP POINT: replace with real NTP-synced time.
    return MOCK_START_UNIX_TIME + (millis() / 1000);
}

int currentLocalHour() {
    if (!g_timezoneInitialized) {
        initClockTimezone("UTC0");
    }
    const time_t now = static_cast<time_t>(currentUnixTimeSec());
    struct tm localTime;
    localtime_compat(&now, &localTime);
    return localTime.tm_hour;
}
