#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "cloud_client.h"
#include "cloud_payload.h"
#include "config.h"
#include "config_store.h"
#include "device_status.h"
#include "hal/clock.h"
#include "hal/moisture_sensor.h"
#include "hal/pump_relay.h"
#include "mqtt_client.h"
#include "pump_runner.h"
#include "reading_buffer.h"
#include "schedule.h"
#include "sleep_manager.h"
#include "sync_scheduler.h"
#include "watchdog.h"
#include "watering_controller.h"
#include "watering_event_buffer.h"
#include "wifi_manager.h"

namespace {
WifiManager g_wifiManager(1000, 30000);

Client& networkClient() {
    if (MQTT_USE_TLS) {
        static WiFiClientSecure secureClient;
        if (MQTT_BROKER_CA_CERT[0] != '\0') secureClient.setCACert(MQTT_BROKER_CA_CERT);
        else secureClient.setInsecure();
        return secureClient;
    }
    static WiFiClient client;
    return client;
}

MqttClient g_mqtt(networkClient(), MQTT_BROKER_HOST, MQTT_BROKER_PORT);
ConfigStore g_configStore;
WateringController g_controller(MOISTURE_THRESHOLD_PCT, MOISTURE_HYSTERESIS_PCT, COOLDOWN_PERIOD_SEC);
Schedule g_schedule(ScheduleConfig{SCHEDULE_WINDOW_START_HOUR, SCHEDULE_WINDOW_END_HOUR, MAX_WATERINGS_PER_DAY});
PumpRunner g_pumpRunner;

RTC_DATA_ATTR uint32_t g_checksSinceLastSync = CHECKS_PER_SYNC - 1;
RTC_DATA_ATTR uint32_t g_nextLocalRequestId = 1;
RTC_DATA_ATTR ReadingBuffer g_readings = {};
RTC_DATA_ATTR WateringEventBuffer g_events = {};
RTC_DATA_ATTR WateringController::State g_controllerState = {true, false, 0};
RTC_DATA_ATTR int g_wateringsToday = 0;
RTC_DATA_ATTR int g_lastCountedDay = -1;

float clampMoisture(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

void recordCompletedRun(const PumpRunner::Result& result) {
    g_controller.notifyWateringComplete(result.completedAtSec);
    g_controllerState = g_controller.getState();
    g_wateringsToday++;
    WateringEventRecord event = {};
    strncpy(event.requestId, result.requestId, sizeof(event.requestId) - 1);
    strncpy(event.trigger, result.trigger, sizeof(event.trigger) - 1);
    strncpy(event.result, "completed", sizeof(event.result) - 1);
    event.requestedDurationSec = result.requestedDurationSec;
    event.actualDurationSec = result.actualDurationSec;
    event.moistureBeforePct = result.moistureBeforePct;
    event.timestampSec = result.completedAtSec;
    wateringEventBufferPush(g_events, event);
}

void runPumpToCompletion() {
    const uint32_t timeoutMs = (g_configStore.minWateringDurationSec() + 10) * 1000UL;
    const uint32_t startedMs = millis();
    PumpRunner::Result result = {};
    PumpRunResult outcome = PumpRunResult::NONE;
    while (g_pumpRunner.isRunning() && millis() - startedMs < timeoutMs) {
        feedWatchdog();
        outcome = g_pumpRunner.update(millis(), currentUnixTimeSec(), result);
        if (outcome == PumpRunResult::COMPLETED) break;
        delay(50);
    }
    if (g_pumpRunner.isRunning()) outcome = g_pumpRunner.stop(millis(), currentUnixTimeSec(), result);
    if (outcome != PumpRunResult::NONE) recordCompletedRun(result);
}

void performCloudSync() {
    const uint32_t startedMs = millis();
    while (g_wifiManager.state() != WifiState::CONNECTED && millis() - startedMs < 20000) {
        feedWatchdog();
        g_wifiManager.update(millis());
        delay(50);
    }
    if (g_wifiManager.state() != WifiState::CONNECTED) return;

    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
    const uint32_t ntpStartedMs = millis();
    while (currentUnixTimeSec() < 1700000000 && millis() - ntpStartedMs < 10000) {
        feedWatchdog();
        delay(50);
    }
    initClockTimezone(TIMEZONE_POSIX);

    if (g_mqtt.connect(DEVICE_ID)) {
        for (size_t i = 0; i < g_readings.count; ++i) {
            g_mqtt.publishMoistureReading(g_readings.moisturePct[i], g_readings.timestampSec[i]);
        }
        for (size_t i = 0; i < g_events.count; ++i) {
            const WateringEventRecord& event = g_events.events[i];
            g_mqtt.publishPumpStatus(event.requestId, event.trigger, event.result,
                                     event.requestedDurationSec, event.actualDurationSec,
                                     event.moistureBeforePct, event.timestampSec);
        }
        g_mqtt.publishDeviceStatus(readWifiRssiDbm(), readBatteryVoltageV(), readUptimeSec(), currentUnixTimeSec());
    }

    char payload[CLOUD_PAYLOAD_MAX_LEN];
    size_t length = buildIngestPayload(payload, sizeof(payload), DEVICE_ID, g_readings, g_events,
                                       readWifiRssiDbm(), readBatteryVoltageV(), readUptimeSec(),
                                       currentUnixTimeSec());
    if (length && sendIngestPayload(payload)) {
        readingBufferClear(g_readings);
        wateringEventBufferClear(g_events);
    }
}
}

void setup() {
    Serial.begin(115200);
    enforceSafeDefaultPumpOff();
    setupWatchdog(45);
    initClockTimezone(TIMEZONE_POSIX);
    g_configStore.load();
    g_controller.configure(g_configStore.moistureThresholdPct(),
                           g_configStore.moistureHysteresisPct(),
                           g_configStore.cooldownPeriodSec());
    g_controller.restoreState(g_controllerState);
    g_schedule.configure(ScheduleConfig{g_configStore.scheduleWindowStartHour(),
                                        g_configStore.scheduleWindowEndHour(),
                                        g_configStore.maxWateringsPerDay()});

    const uint32_t nowSec = currentUnixTimeSec();
    const int today = static_cast<int>(nowSec / 86400);
    if (today != g_lastCountedDay) {
        g_wateringsToday = 0;
        g_lastCountedDay = today;
    }

    const float moisturePct = clampMoisture(readMoisturePercent());
    readingBufferPush(g_readings, moisturePct, nowSec);
    const WaterDecision decision = g_controller.evaluate(moisturePct, nowSec);
    g_controllerState = g_controller.getState();
    if (decision == WaterDecision::WATER_TRIGGERED &&
        g_schedule.allowsWatering(currentLocalHour(), g_wateringsToday)) {
        char requestId[32];
        snprintf(requestId, sizeof(requestId), "local_%lu", static_cast<unsigned long>(g_nextLocalRequestId++));
        if (g_pumpRunner.start(millis(), nowSec, g_configStore.minWateringDurationSec(),
                               requestId, "moisture", moisturePct)) runPumpToCompletion();
    }

    if (syncDueThisWake(g_checksSinceLastSync, CHECKS_PER_SYNC)) performCloudSync();
    enterDeepSleep(CHECK_INTERVAL_SEC);
}

void loop() {}
