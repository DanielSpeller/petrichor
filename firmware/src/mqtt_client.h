#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

// Thin wrapper around PubSubClient publishing/subscribing on the exact
// topics and payload shapes defined in SPEC.md §1. REQUIRES REAL HARDWARE
// (WiFiClient/TCP) to actually connect to a broker -- only builds under
// the esp32dev environment.
//
// KNOWN LIMITATION: PubSubClient always sends publishes at QoS 0 on the
// wire, regardless of the QoS SPEC.md specifies (QoS 1 for
// garden/pump/command and garden/pump/status). Subscriptions can request
// QoS 1 in the SUBSCRIBE packet, but true at-least-once publish
// guarantees will need either a different MQTT library or
// application-level ack handling once real hardware exists. Documented
// here so it isn't silently forgotten.
class MqttClient {
public:
    MqttClient(Client& networkClient, const char* brokerHost, uint16_t brokerPort);

    bool connect(const char* clientId);
    void loop();
    bool isConnected();

    // Publishes a garden/sensor/moisture reading. Not retained, per SPEC.md.
    bool publishMoistureReading(float moisturePct, uint32_t timestamp);

    // Publishes a garden/pump/status confirmation. Not retained, per SPEC.md.
    bool publishPumpStatus(const char* requestId, const char* trigger, const char* result,
                            uint32_t requestedDurationSec, uint32_t actualDurationSec,
                            float moistureBeforePct, uint32_t timestamp);

    // Publishes a garden/pump/ack application-level acknowledgment. Not retained.
    // Used because PubSubClient cannot send true MQTT QoS 1 publishes; the Pi
    // correlates this ack with its command request_id to know the command arrived.
    bool publishPumpAck(const char* requestId, uint32_t timestamp);

    // Publishes a garden/device/status heartbeat. Retained, per SPEC.md.
    bool publishDeviceStatus(int wifiRssiDbm, float supplyVoltageV, uint32_t uptimeSec, uint32_t timestamp);

    // Subscribes to garden/pump/command with the given callback.
    bool subscribeToPumpCommand(MQTT_CALLBACK_SIGNATURE);

private:
    PubSubClient client_;
};
