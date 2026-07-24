#include "hal/pump_relay.h"

namespace {
bool g_relayOn = false; // safe default: OFF
}

void setPumpRelay(bool on) {
    g_relayOn = on;
}

bool getPumpRelayState() {
    return g_relayOn;
}
