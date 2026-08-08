#pragma once

#include <cstdint>

// HARDWARE SWAP POINT: replace both bodies in clock.cpp with real
// NTP-synced time (e.g. via configTime() + time(nullptr)) when hardware
// exists. Currently mocked: unix time starts from a fixed epoch and
// advances with millis(); local hour uses the configured POSIX timezone.
void initClockTimezone(const char* tzPosix);
uint32_t currentUnixTimeSec();
int currentLocalHour();
