#include <cstring>
#include <unity.h>
#include "watering_event_buffer.h"

void setUp(void) {}
void tearDown(void) {}

WateringEventRecord record(const char* id) {
    WateringEventRecord value = {};
    strncpy(value.requestId, id, sizeof(value.requestId) - 1);
    return value;
}

void test_watering_event_buffer(void) {
    WateringEventBuffer buffer = {};
    for (size_t i = 0; i < WATERING_EVENT_BUFFER_CAPACITY; ++i) {
        wateringEventBufferPush(buffer, record("old"));
    }
    wateringEventBufferPush(buffer, record("new"));
    TEST_ASSERT_EQUAL(WATERING_EVENT_BUFFER_CAPACITY, buffer.count);
    TEST_ASSERT_EQUAL_STRING("new", buffer.events[WATERING_EVENT_BUFFER_CAPACITY - 1].requestId);
    wateringEventBufferClear(buffer);
    TEST_ASSERT_EQUAL(0, buffer.count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_watering_event_buffer);
    return UNITY_END();
}
