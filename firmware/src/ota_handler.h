#pragma once

// OTA update handling.
//
// Uses ArduinoOTA on the ESP32 target so the device can be reflashed over
// WiFi once installed. The native test build has no OTA support; setupOta()
// and handleOta() are no-ops there.

void setupOta(const char* password);
void handleOta();
