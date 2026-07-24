#pragma once

// Soil moisture HAL. Returns a percentage in [0.0, 100.0], matching
// SPEC.md's convention (never raw ADC counts).
//
// HARDWARE SWAP POINT: moisture_sensor.cpp currently returns mock values
// (scripted or randomized). When real hardware is available, replace the
// body of readMoisturePercent() with an analogRead(pin) call converted to
// a percentage -- nothing else in the firmware calls analogRead()
// directly, so this is the only function that needs to change. Note that
// doing so will pull in <Arduino.h>, so this file will no longer build
// under the `native` PlatformIO environment -- that's expected once real
// hardware exists.
float readMoisturePercent();

// Test-only hook. Makes readMoisturePercent() return values from `values`
// in order (repeating the last value once exhausted) instead of
// randomized mock values. Pass count == 0 (or values == nullptr) to
// revert to randomized mock values.
void setScriptedMoistureSequence(const float* values, int count);
