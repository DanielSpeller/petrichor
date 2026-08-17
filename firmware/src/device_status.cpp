#include "device_status.h"
#include <Arduino.h>

int readWifiRssiDbm() {
    // HARDWARE SWAP POINT: replace with WiFi.RSSI().
    return -60; // mocked, plausible mid-strength signal
}

float readSupplyVoltageV() {
    // HARDWARE SWAP POINT: replace with a documented low-voltage rail ADC read.
    return 0.0f; // V1 has no supply monitor; 0.0 is encoded as null by the payload builders.
}

uint32_t readUptimeSec() {
    return millis() / 1000;
}
