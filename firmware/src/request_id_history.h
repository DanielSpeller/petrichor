#pragma once

#include <cstdint>
#include <cstring>

// Ring buffer of recent MQTT request_ids used for command deduplication.
//
// PubSubClient cannot publish at true MQTT QoS 1, so the firmware adds an
// application-layer at-least-once contract: the Pi may retry a command, and
// the ESP32 must not execute the same request_id twice. A received duplicate
// is acknowledged again on garden/pump/ack but is not dispatched to the pump.
//
// HISTORY_SIZE tunes memory vs. how long a request_id remains dedup-able.
// With one command every few seconds, 16 entries covers several minutes of
// retries, which is more than enough for a single-watering use case.
class RequestIdHistory {
public:
    static constexpr size_t MAX_REQUEST_ID_LEN = 40;
    static constexpr size_t HISTORY_SIZE = 16;

    RequestIdHistory();

    // Returns true if this request_id has been seen before (and is therefore
    // a duplicate). Returns false for a new request_id, which is then stored.
    bool isDuplicate(const char* requestId);

    // Explicitly clear all stored entries. Useful in tests.
    void clear();

private:
    char history_[HISTORY_SIZE][MAX_REQUEST_ID_LEN + 1];
    size_t nextIndex_;
};
