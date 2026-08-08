#include "watering_event_buffer.h"

void wateringEventBufferPush(WateringEventBuffer& buffer, const WateringEventRecord& record) {
    if (buffer.count < WATERING_EVENT_BUFFER_CAPACITY) {
        buffer.events[buffer.count++] = record;
        return;
    }
    for (size_t i = 1; i < WATERING_EVENT_BUFFER_CAPACITY; ++i) {
        buffer.events[i - 1] = buffer.events[i];
    }
    buffer.events[WATERING_EVENT_BUFFER_CAPACITY - 1] = record;
}

void wateringEventBufferClear(WateringEventBuffer& buffer) { buffer.count = 0; }
