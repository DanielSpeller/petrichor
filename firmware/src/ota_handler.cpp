#include "ota_handler.h"

#ifdef ARDUINO
#include <ArduinoOTA.h>

void setupOta(const char* password) {
    if (password && password[0] != '\0') {
        ArduinoOTA.setPassword(password);
    }
    ArduinoOTA.begin();
}

void handleOta() {
    ArduinoOTA.handle();
}

#else

void setupOta(const char*) {}
void handleOta() {}

#endif
