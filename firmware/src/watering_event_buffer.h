#pragma once

#include <cstddef>
#include <cstdint>

constexpr size_t WATERING_EVENT_BUFFER_CAPACITY = 8;
constexpr size_t WATERING_EVENT_MAX_REQUEST_ID_LEN = 36;
constexpr size_t WATERING_EVENT_MAX_TRIGGER_LEN = 15;
constexpr size_t WATERING_EVENT_MAX_RESULT_LEN = 15;

struct WateringEventRecord {
    char requestId[WATERING_EVENT_MAX_REQUEST_ID_LEN + 1];
    char trigger[WATERING_EVENT_MAX_TRIGGER_LEN + 1];
    char result[WATERING_EVENT_MAX_RESULT_LEN + 1];
    uint32_t requestedDurationSec;
    uint32_t actualDurationSec;
    float moistureBeforePct;
    uint32_t timestampSec;
};

struct WateringEventBuffer {
    WateringEventRecord events[WATERING_EVENT_BUFFER_CAPACITY];
    size_t count;
};

void wateringEventBufferPush(WateringEventBuffer& buffer, const WateringEventRecord& record);
void wateringEventBufferClear(WateringEventBuffer& buffer);
