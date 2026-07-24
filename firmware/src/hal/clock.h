#pragma once

#include <cstdint>

// HARDWARE SWAP POINT: replace both bodies in clock.cpp with real
// NTP-synced time (e.g. via configTime() + time(nullptr)) when hardware
// exists. Currently mocked: unix time starts from a fixed epoch and
// advances with millis(); local hour is derived from that as UTC (no real
// timezone handling yet).
uint32_t currentUnixTimeSec();
int currentLocalHour();
