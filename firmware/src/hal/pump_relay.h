#pragma once

// Pump relay HAL. `on == true` energizes the relay (pump running).
//
// HARDWARE SWAP POINT: pump_relay.cpp currently just tracks state in
// memory (mock). When real hardware is available, replace the body of
// setPumpRelay() with a digitalWrite(PUMP_RELAY_PIN, on ? HIGH : LOW)
// call -- nothing else in the firmware writes the pump pin directly, so
// this is the only function that needs to change.
void setPumpRelay(bool on);

// Current mock relay state. Used by watchdog logic and tests to assert
// the pump defaults to OFF.
bool getPumpRelayState();
