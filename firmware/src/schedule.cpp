#include "schedule.h"

Schedule::Schedule(const ScheduleConfig& config) : config_(config) {}

bool Schedule::isWithinWindow(int localHour) const {
    return localHour >= config_.windowStartHour && localHour < config_.windowEndHour;
}

bool Schedule::isUnderDailyLimit(int wateringsSoFarToday) const {
    return wateringsSoFarToday < config_.maxWateringsPerDay;
}

bool Schedule::allowsWatering(int localHour, int wateringsSoFarToday) const {
    return isWithinWindow(localHour) && isUnderDailyLimit(wateringsSoFarToday);
}
