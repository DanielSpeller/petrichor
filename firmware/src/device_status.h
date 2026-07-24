#pragma once

#include <cstdint>

// Heartbeat data for garden/device/status (SPEC.md §1). Values are read
// via these functions so main.cpp doesn't need to know whether they're
// real or mocked.
//
// HARDWARE SWAP POINT: replace the bodies of readWifiRssiDbm() and
// readBatteryVoltageV() in device_status.cpp with WiFi.RSSI() and a real
// battery ADC read when hardware exists. readUptimeSec() is already real
// (millis()-based) and needs no swap.
int readWifiRssiDbm();
float readBatteryVoltageV();
uint32_t readUptimeSec();
