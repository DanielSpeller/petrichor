#include "device_status.h"
#include <Arduino.h>

int readWifiRssiDbm() {
    // HARDWARE SWAP POINT: replace with WiFi.RSSI().
    return -60; // mocked, plausible mid-strength signal
}

float readBatteryVoltageV() {
    // HARDWARE SWAP POINT: replace with a real ADC read + voltage divider math.
    return 3.98f; // mocked, plausible LiPo voltage
}

uint32_t readUptimeSec() {
    return millis() / 1000;
}
