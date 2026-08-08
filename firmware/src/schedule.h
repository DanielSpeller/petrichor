#pragma once

struct ScheduleConfig {
    int windowStartHour; // local hour, 0-23, inclusive
    int windowEndHour;   // local hour, 0-23, exclusive
    int maxWateringsPerDay;
};

// Pure logic, no Arduino/network dependency -- testable under `native`.
class Schedule {
public:
    explicit Schedule(const ScheduleConfig& config);

    // Update schedule config at runtime (e.g. after loading persisted config).
    void configure(const ScheduleConfig& config);

    // True if `localHour` (0-23) falls inside the allowed watering window.
    bool isWithinWindow(int localHour) const;

    // True if `wateringsSoFarToday` is still under the daily cap.
    bool isUnderDailyLimit(int wateringsSoFarToday) const;

    // Combines both checks -- what main.cpp calls before acting on a
    // WaterDecision::WATER_TRIGGERED from WateringController. The
    // schedule can only veto a moisture trigger; it never creates one on
    // its own.
    bool allowsWatering(int localHour, int wateringsSoFarToday) const;

private:
    ScheduleConfig config_;
};
