#pragma once

#include <cstdint>

// Heartbeat data for garden/device/status (SPEC.md §1). Values are read
// via these functions so main.cpp doesn't need to know whether they're
// real or mocked.
//
// HARDWARE SWAP POINT: replace the bodies of readWifiRssiDbm() and
// readSupplyVoltageV() in device_status.cpp with WiFi.RSSI() and a documented
// low-voltage supply ADC read when a later indoor profile requires it.
// readUptimeSec() is already real
// (millis()-based) and needs no swap.
int readWifiRssiDbm();
float readSupplyVoltageV();
uint32_t readUptimeSec();
