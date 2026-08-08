#include "config_store.h"
#include "config.h"

#ifdef ARDUINO
#include <Preferences.h>
#endif

namespace {
constexpr char kNamespace[] = "petrichor";
constexpr char kMoistureThresholdPct[] = "moist_thres";
constexpr char kMoistureHysteresisPct[] = "moist_hyst";
constexpr char kMinWateringDurationSec[] = "min_dur";
constexpr char kCooldownPeriodSec[] = "cooldown";
constexpr char kScheduleWindowStartHour[] = "win_start";
constexpr char kScheduleWindowEndHour[] = "win_end";
constexpr char kMaxWateringsPerDay[] = "max_day";
}

ConfigStore::ConfigStore()
    : moistureThresholdPct_(MOISTURE_THRESHOLD_PCT),
      moistureHysteresisPct_(MOISTURE_HYSTERESIS_PCT),
      minWateringDurationSec_(MIN_WATERING_DURATION_SEC),
      cooldownPeriodSec_(COOLDOWN_PERIOD_SEC),
      scheduleWindowStartHour_(SCHEDULE_WINDOW_START_HOUR),
      scheduleWindowEndHour_(SCHEDULE_WINDOW_END_HOUR),
      maxWateringsPerDay_(MAX_WATERINGS_PER_DAY) {}

void ConfigStore::load() {
#ifdef ARDUINO
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        resetToDefaults();
        return;
    }

    moistureThresholdPct_ = prefs.getFloat(kMoistureThresholdPct, MOISTURE_THRESHOLD_PCT);
    moistureHysteresisPct_ = prefs.getFloat(kMoistureHysteresisPct, MOISTURE_HYSTERESIS_PCT);
    minWateringDurationSec_ = prefs.getULong(kMinWateringDurationSec, MIN_WATERING_DURATION_SEC);
    cooldownPeriodSec_ = prefs.getULong(kCooldownPeriodSec, COOLDOWN_PERIOD_SEC);
    scheduleWindowStartHour_ = prefs.getInt(kScheduleWindowStartHour, SCHEDULE_WINDOW_START_HOUR);
    scheduleWindowEndHour_ = prefs.getInt(kScheduleWindowEndHour, SCHEDULE_WINDOW_END_HOUR);
    maxWateringsPerDay_ = prefs.getInt(kMaxWateringsPerDay, MAX_WATERINGS_PER_DAY);

    prefs.end();
#else
    resetToDefaults();
#endif
}

void ConfigStore::resetToDefaults() {
    moistureThresholdPct_ = MOISTURE_THRESHOLD_PCT;
    moistureHysteresisPct_ = MOISTURE_HYSTERESIS_PCT;
    minWateringDurationSec_ = MIN_WATERING_DURATION_SEC;
    cooldownPeriodSec_ = COOLDOWN_PERIOD_SEC;
    scheduleWindowStartHour_ = SCHEDULE_WINDOW_START_HOUR;
    scheduleWindowEndHour_ = SCHEDULE_WINDOW_END_HOUR;
    maxWateringsPerDay_ = MAX_WATERINGS_PER_DAY;

#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putFloat(kMoistureThresholdPct, moistureThresholdPct_);
        prefs.putFloat(kMoistureHysteresisPct, moistureHysteresisPct_);
        prefs.putULong(kMinWateringDurationSec, minWateringDurationSec_);
        prefs.putULong(kCooldownPeriodSec, cooldownPeriodSec_);
        prefs.putInt(kScheduleWindowStartHour, scheduleWindowStartHour_);
        prefs.putInt(kScheduleWindowEndHour, scheduleWindowEndHour_);
        prefs.putInt(kMaxWateringsPerDay, maxWateringsPerDay_);
        prefs.end();
    }
#endif
}

float ConfigStore::moistureThresholdPct() const { return moistureThresholdPct_; }
float ConfigStore::moistureHysteresisPct() const { return moistureHysteresisPct_; }
uint32_t ConfigStore::minWateringDurationSec() const { return minWateringDurationSec_; }
uint32_t ConfigStore::cooldownPeriodSec() const { return cooldownPeriodSec_; }
int ConfigStore::scheduleWindowStartHour() const { return scheduleWindowStartHour_; }
int ConfigStore::scheduleWindowEndHour() const { return scheduleWindowEndHour_; }
int ConfigStore::maxWateringsPerDay() const { return maxWateringsPerDay_; }

void ConfigStore::setMoistureThresholdPct(float value) {
    moistureThresholdPct_ = value;
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putFloat(kMoistureThresholdPct, value);
        prefs.end();
    }
#endif
}

void ConfigStore::setMoistureHysteresisPct(float value) {
    moistureHysteresisPct_ = value;
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putFloat(kMoistureHysteresisPct, value);
        prefs.end();
    }
#endif
}

void ConfigStore::setMinWateringDurationSec(uint32_t value) {
    minWateringDurationSec_ = value;
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putULong(kMinWateringDurationSec, value);
        prefs.end();
    }
#endif
}

void ConfigStore::setCooldownPeriodSec(uint32_t value) {
    cooldownPeriodSec_ = value;
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putULong(kCooldownPeriodSec, value);
        prefs.end();
    }
#endif
}

void ConfigStore::setScheduleWindowStartHour(int value) {
    scheduleWindowStartHour_ = value;
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putInt(kScheduleWindowStartHour, value);
        prefs.end();
    }
#endif
}

void ConfigStore::setScheduleWindowEndHour(int value) {
    scheduleWindowEndHour_ = value;
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putInt(kScheduleWindowEndHour, value);
        prefs.end();
    }
#endif
}

void ConfigStore::setMaxWateringsPerDay(int value) {
    maxWateringsPerDay_ = value;
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
        prefs.putInt(kMaxWateringsPerDay, value);
        prefs.end();
    }
#endif
}
