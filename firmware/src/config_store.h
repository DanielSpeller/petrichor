#pragma once

#include <cstdint>

// Runtime configuration store.
//
// On the ESP32 this is backed by ESP32 Preferences (NVS), so thresholds,
// schedule, and related values survive reboots and can be changed without
// reflashing. On the native test build Preferences is unavailable, so the
// store keeps values in memory with compile-time defaults from config.h.
//
// Keys match the compile-time constant names so the mapping is obvious.
class ConfigStore {
public:
    ConfigStore();

    // Load from persistent storage (or reset to defaults if missing).
    void load();

    // Reset all values to the compile-time defaults.
    void resetToDefaults();

    float moistureThresholdPct() const;
    float moistureHysteresisPct() const;
    uint32_t minWateringDurationSec() const;
    uint32_t cooldownPeriodSec() const;
    int scheduleWindowStartHour() const;
    int scheduleWindowEndHour() const;
    int maxWateringsPerDay() const;

    void setMoistureThresholdPct(float value);
    void setMoistureHysteresisPct(float value);
    void setMinWateringDurationSec(uint32_t value);
    void setCooldownPeriodSec(uint32_t value);
    void setScheduleWindowStartHour(int value);
    void setScheduleWindowEndHour(int value);
    void setMaxWateringsPerDay(int value);

private:
    float moistureThresholdPct_;
    float moistureHysteresisPct_;
    uint32_t minWateringDurationSec_;
    uint32_t cooldownPeriodSec_;
    int scheduleWindowStartHour_;
    int scheduleWindowEndHour_;
    int maxWateringsPerDay_;
};
