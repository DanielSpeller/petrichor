#include <Arduino.h>
#include <WiFi.h>

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

namespace {
WifiManager g_wifiManager(1000, 30000);
WiFiClient g_networkClient;
// Local Mosquitto broker address -- update to match your laptop's IP/hostname.
MqttClient g_mqtt(g_networkClient, "192.168.1.100", 1883);
WateringController g_wateringController(MOISTURE_THRESHOLD_PCT, MOISTURE_HYSTERESIS_PCT, COOLDOWN_PERIOD_SEC);
Schedule g_schedule(ScheduleConfig{SCHEDULE_WINDOW_START_HOUR, SCHEDULE_WINDOW_END_HOUR, MAX_WATERINGS_PER_DAY});

int g_wateringsToday = 0;
int g_lastCountedDay = -1;

uint32_t g_lastHeartbeatMs = 0;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 60000;

uint32_t g_lastMoistureReadMs = 0;
constexpr uint32_t MOISTURE_READ_INTERVAL_MS = 10000;

void onPumpCommand(char* topic, uint8_t* payload, unsigned int length) {
    // Minimal stub for the remote-prep phase: no hardware exists to
    // actually act on a run/stop command yet, and JSON parsing needs a
    // library (e.g. ArduinoJson) not yet added as a dependency. Full
    // command handling (device_id match, run/stop, duration_sec) is a
    // follow-up once real hardware/broker testing begins.
    (void)topic;
    (void)payload;
    (void)length;
}
}

void setup() {
    Serial.begin(115200);
    enforceSafeDefaultPumpOff();
    setupWatchdog(30);
}

void loop() {
    feedWatchdog();

    uint32_t nowMs = millis();
    g_wifiManager.update(nowMs);

    if (g_wifiManager.state() == WifiState::CONNECTED && !g_mqtt.isConnected()) {
        g_mqtt.connect(DEVICE_ID);
        g_mqtt.subscribeToPumpCommand(onPumpCommand);
    }
    if (g_mqtt.isConnected()) {
        g_mqtt.loop();
    }

    uint32_t nowSec = currentUnixTimeSec();
    int today = static_cast<int>(nowSec / 86400);
    if (today != g_lastCountedDay) {
        g_wateringsToday = 0;
        g_lastCountedDay = today;
    }

    if (nowMs - g_lastMoistureReadMs >= MOISTURE_READ_INTERVAL_MS) {
        g_lastMoistureReadMs = nowMs;
        float moisturePct = readMoisturePercent();

        if (g_mqtt.isConnected()) {
            g_mqtt.publishMoistureReading(moisturePct, nowSec);
        }

        WaterDecision decision = g_wateringController.evaluate(moisturePct, nowSec);
        if (decision == WaterDecision::WATER_TRIGGERED &&
            g_schedule.allowsWatering(currentLocalHour(), g_wateringsToday)) {
            // SIMPLIFICATION: this blocks for the run duration, so an
            // incoming "stop" command can't interrupt it and MQTT/watchdog
            // servicing pauses during the run. A non-blocking state
            // machine (track pump-start time, check elapsed each loop) is
            // the natural next step once hardware exists and "stop early"
            // needs to actually work.
            setPumpRelay(true);
            delay(MIN_WATERING_DURATION_SEC * 1000UL);
            setPumpRelay(false);

            uint32_t completedAt = currentUnixTimeSec();
            g_wateringController.notifyWateringComplete(completedAt);
            g_wateringsToday++;

            if (g_mqtt.isConnected()) {
                g_mqtt.publishPumpStatus("local-trigger", "moisture", "completed",
                                          MIN_WATERING_DURATION_SEC, MIN_WATERING_DURATION_SEC,
                                          moisturePct, completedAt);
            }
        }
    }

    if (nowMs - g_lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
        g_lastHeartbeatMs = nowMs;
        if (g_mqtt.isConnected()) {
            g_mqtt.publishDeviceStatus(readWifiRssiDbm(), readBatteryVoltageV(), readUptimeSec(), currentUnixTimeSec());
        }
    }
}
