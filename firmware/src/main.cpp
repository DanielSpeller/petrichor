#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "config.h"
#include "hal/moisture_sensor.h"
#include "hal/pump_relay.h"
#include "hal/clock.h"
#include "watering_controller.h"
#include "schedule.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "device_status.h"
#include "watchdog.h"
#include "pump_runner.h"
#include "request_id_history.h"
#include "config_store.h"
#include "ota_handler.h"

namespace {
WifiManager g_wifiManager(1000, 30000);

Client& networkClient() {
    if (MQTT_USE_TLS) {
        static WiFiClientSecure instance;
        if (MQTT_BROKER_CA_CERT[0] != '\0') {
            instance.setCACert(MQTT_BROKER_CA_CERT);
        } else {
            instance.setInsecure();
        }
        return instance;
    }
    static WiFiClient instance;
    return instance;
}

MqttClient g_mqtt(networkClient(), MQTT_BROKER_HOST, MQTT_BROKER_PORT);
ConfigStore g_configStore;
WateringController g_wateringController(
    g_configStore.moistureThresholdPct(),
    g_configStore.moistureHysteresisPct(),
    g_configStore.cooldownPeriodSec());
Schedule g_schedule(ScheduleConfig{
    g_configStore.scheduleWindowStartHour(),
    g_configStore.scheduleWindowEndHour(),
    g_configStore.maxWateringsPerDay()});
PumpRunner g_pumpRunner;
RequestIdHistory g_commandRequestIdHistory;

int g_wateringsToday = 0;
int g_lastCountedDay = -1;

uint32_t g_lastHeartbeatMs = 0;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 60000;

uint32_t g_lastMoistureReadMs = 0;
constexpr uint32_t MOISTURE_READ_INTERVAL_MS = 10000;

uint32_t g_nextLocalRequestId = 1;

// Buffer for a pump/status message that could not be published immediately
// (e.g. broker disconnected mid-run). Retried each loop once reconnected.
bool g_pendingPumpStatus = false;
PumpRunner::Result g_pendingPumpStatusResult;
char g_pendingPumpStatusResultStr[16] = {};

void makeLocalRequestId(char* buffer, size_t size) {
    snprintf(buffer, size, "local_%lu", static_cast<unsigned long>(g_nextLocalRequestId++));
}

float clampMoisture(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 100.0f) return 100.0f;
    return value;
}

bool publishPumpStatusFromResult(const PumpRunner::Result& result, const char* resultStr) {
    if (!g_mqtt.isConnected()) {
        return false;
    }
    return g_mqtt.publishPumpStatus(result.requestId, result.trigger, resultStr,
                                    result.requestedDurationSec, result.actualDurationSec,
                                    result.moistureBeforePct, result.completedAtSec);
}

void bufferPendingPumpStatus(const PumpRunner::Result& result, const char* resultStr) {
    g_pendingPumpStatusResult = result;
    strncpy(g_pendingPumpStatusResultStr, resultStr, sizeof(g_pendingPumpStatusResultStr) - 1);
    g_pendingPumpStatusResultStr[sizeof(g_pendingPumpStatusResultStr) - 1] = '\0';
    g_pendingPumpStatus = true;
}

void publishOrBufferPumpStatus(const PumpRunner::Result& result, const char* resultStr) {
    if (!publishPumpStatusFromResult(result, resultStr)) {
        bufferPendingPumpStatus(result, resultStr);
    }
}

void handleCompletedRun(const PumpRunner::Result& result) {
    g_wateringController.notifyWateringComplete(result.completedAtSec);
    g_wateringsToday++;
    publishOrBufferPumpStatus(result, "completed");
}

bool publishSkippedStatus(const char* requestId, const char* trigger,
                          uint32_t requestedDurationSec, float moistureBeforePct) {
    if (!g_mqtt.isConnected()) {
        return false;
    }
    return g_mqtt.publishPumpStatus(requestId, trigger, "skipped",
                                    requestedDurationSec, 0,
                                    moistureBeforePct, currentUnixTimeSec());
}

void onPumpCommand(char* topic, uint8_t* payload, unsigned int length) {
    (void)topic;

    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err != DeserializationError::Ok) {
        return;
    }

    const char* deviceId = doc["device_id"];
    if (!deviceId || strcmp(deviceId, DEVICE_ID) != 0) {
        return;
    }

    const char* command = doc["command"];
    if (!command) {
        return;
    }

    const char* providedRequestId = doc["request_id"];
    char localRequestIdBuffer[32];
    if (!providedRequestId || providedRequestId[0] == '\0') {
        makeLocalRequestId(localRequestIdBuffer, sizeof(localRequestIdBuffer));
        providedRequestId = localRequestIdBuffer;
    }
    const char* requestId = providedRequestId;
    const uint32_t nowSec = currentUnixTimeSec();

    // Application-level QoS 1: acknowledge every valid command immediately.
    // A duplicate request_id is acked again but not re-executed below.
    if (g_mqtt.isConnected()) {
        g_mqtt.publishPumpAck(requestId, nowSec);
    }

    if (g_commandRequestIdHistory.isDuplicate(requestId)) {
        return;
    }

    if (strcmp(command, "run") == 0) {
        const uint32_t minDurationSec = g_configStore.minWateringDurationSec();
        uint32_t durationSec = doc["duration_sec"] | minDurationSec;
        if (durationSec < minDurationSec) {
            durationSec = minDurationSec;
        }

        const char* trigger = doc["trigger"] | "manual";

        float moistureBeforePct = clampMoisture(readMoisturePercent());
        if (g_pumpRunner.isRunning()) {
            publishSkippedStatus(requestId, trigger, durationSec, moistureBeforePct);
            return;
        }

        g_pumpRunner.start(millis(), nowSec, durationSec, requestId, trigger, moistureBeforePct);
    } else if (strcmp(command, "stop") == 0) {
        PumpRunner::Result result;
        if (g_pumpRunner.stop(millis(), nowSec, result) == PumpRunResult::STOPPED_EARLY) {
            handleCompletedRun(result);
        }
    }
}
}

void setup() {
    Serial.begin(115200);
    enforceSafeDefaultPumpOff();
    setupWatchdog(30);

    initClockTimezone(TIMEZONE_POSIX);

    setupOta(OTA_PASSWORD);

    g_configStore.load();
    g_wateringController.configure(
        g_configStore.moistureThresholdPct(),
        g_configStore.moistureHysteresisPct(),
        g_configStore.cooldownPeriodSec());
    g_schedule.configure(ScheduleConfig{
        g_configStore.scheduleWindowStartHour(),
        g_configStore.scheduleWindowEndHour(),
        g_configStore.maxWateringsPerDay()});
}

void loop() {
    feedWatchdog();
    handleOta();

    uint32_t nowMs = millis();
    g_wifiManager.update(nowMs);

    if (g_wifiManager.state() == WifiState::CONNECTED && !g_mqtt.isConnected()) {
        g_mqtt.connect(DEVICE_ID);
        g_mqtt.subscribeToPumpCommand(onPumpCommand);
    }
    if (g_mqtt.isConnected()) {
        g_mqtt.loop();
    }

    // Retry any pump/status message that failed to publish earlier.
    if (g_pendingPumpStatus && publishPumpStatusFromResult(g_pendingPumpStatusResult, g_pendingPumpStatusResultStr)) {
        g_pendingPumpStatus = false;
    }

    // Update the non-blocking pump state machine and publish status when a
    // run finishes (either by elapsed time or by an external stop command).
    PumpRunner::Result result;
    PumpRunResult runResult = g_pumpRunner.update(nowMs, currentUnixTimeSec(), result);
    if (runResult == PumpRunResult::COMPLETED || runResult == PumpRunResult::STOPPED_EARLY) {
        handleCompletedRun(result);
    }

    uint32_t nowSec = currentUnixTimeSec();
    int today = static_cast<int>(nowSec / 86400);
    if (today != g_lastCountedDay) {
        g_wateringsToday = 0;
        g_lastCountedDay = today;
    }

    if (nowMs - g_lastMoistureReadMs >= MOISTURE_READ_INTERVAL_MS) {
        g_lastMoistureReadMs = nowMs;
        float moisturePct = clampMoisture(readMoisturePercent());

        if (g_mqtt.isConnected()) {
            g_mqtt.publishMoistureReading(moisturePct, nowSec);
        }

        WaterDecision decision = g_wateringController.evaluate(moisturePct, nowSec);
        if (decision == WaterDecision::WATER_TRIGGERED &&
            g_schedule.allowsWatering(currentLocalHour(), g_wateringsToday) &&
            !g_pumpRunner.isRunning()) {
            char requestId[32];
            makeLocalRequestId(requestId, sizeof(requestId));
            g_pumpRunner.start(nowMs, nowSec, g_configStore.minWateringDurationSec(), requestId, "moisture", moisturePct);
        }
    }

    if (nowMs - g_lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
        g_lastHeartbeatMs = nowMs;
        if (g_mqtt.isConnected()) {
            g_mqtt.publishDeviceStatus(readWifiRssiDbm(), readBatteryVoltageV(), readUptimeSec(), currentUnixTimeSec());
        }
    }
}
