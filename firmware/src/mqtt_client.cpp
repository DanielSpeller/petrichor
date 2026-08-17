#include "mqtt_client.h"
#include "config.h"

#include <cstdio>

MqttClient::MqttClient(Client& networkClient, const char* brokerHost, uint16_t brokerPort)
    : client_(networkClient) {
    client_.setServer(brokerHost, brokerPort);
}

bool MqttClient::connect(const char* clientId) {
    return client_.connect(clientId);
}

void MqttClient::loop() {
    client_.loop();
}

bool MqttClient::isConnected() {
    return client_.connected();
}

bool MqttClient::publishMoistureReading(float moisturePct, uint32_t timestamp) {
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"moisture_pct\":%.1f,\"timestamp\":%lu}",
             DEVICE_ID, moisturePct, static_cast<unsigned long>(timestamp));
    return client_.publish(TOPIC_SENSOR_MOISTURE, payload, false);
}

bool MqttClient::publishPumpStatus(const char* requestId, const char* trigger, const char* result,
                                    uint32_t requestedDurationSec, uint32_t actualDurationSec,
                                    float moistureBeforePct, uint32_t timestamp) {
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"request_id\":\"%s\",\"trigger\":\"%s\",\"result\":\"%s\","
             "\"requested_duration_sec\":%lu,\"actual_duration_sec\":%lu,"
             "\"moisture_before_pct\":%.1f,\"timestamp\":%lu}",
             DEVICE_ID, requestId, trigger, result,
             static_cast<unsigned long>(requestedDurationSec),
             static_cast<unsigned long>(actualDurationSec),
             moistureBeforePct, static_cast<unsigned long>(timestamp));
    return client_.publish(TOPIC_PUMP_STATUS, payload, false);
}

bool MqttClient::publishPumpAck(const char* requestId, uint32_t timestamp) {
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"device_id\":\"%s\",\"request_id\":\"%s\",\"timestamp\":%lu}",
             DEVICE_ID, requestId, static_cast<unsigned long>(timestamp));
    return client_.publish(TOPIC_PUMP_ACK, payload, false);
}

bool MqttClient::publishDeviceStatus(int wifiRssiDbm, float supplyVoltageV, uint32_t uptimeSec, uint32_t timestamp) {
    char payload[192];
    if (supplyVoltageV > 0.0f) {
        snprintf(payload, sizeof(payload),
                 "{\"device_id\":\"%s\",\"wifi_rssi_dbm\":%d,\"supply_voltage_v\":%.2f,"
                 "\"uptime_sec\":%lu,\"timestamp\":%lu}",
                 DEVICE_ID, wifiRssiDbm, supplyVoltageV,
                 static_cast<unsigned long>(uptimeSec), static_cast<unsigned long>(timestamp));
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"device_id\":\"%s\",\"wifi_rssi_dbm\":%d,\"supply_voltage_v\":null,"
                 "\"uptime_sec\":%lu,\"timestamp\":%lu}",
                 DEVICE_ID, wifiRssiDbm,
                 static_cast<unsigned long>(uptimeSec), static_cast<unsigned long>(timestamp));
    }
    return client_.publish(TOPIC_DEVICE_STATUS, payload, true); // retained, per SPEC.md
}

bool MqttClient::subscribeToPumpCommand(MQTT_CALLBACK_SIGNATURE) {
    client_.setCallback(callback);
    return client_.subscribe(TOPIC_PUMP_COMMAND, 1);
}
