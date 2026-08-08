#include <cstring>
#include <unity.h>
#include "cloud_payload.h"

void setUp(void) {}
void tearDown(void) {}

void test_cloud_payload(void) {
    ReadingBuffer readings = {};
    readingBufferPush(readings, 42.5f, 1000);
    WateringEventBuffer events = {};
    WateringEventRecord event = {};
    strncpy(event.requestId, "local_1", sizeof(event.requestId) - 1);
    strncpy(event.trigger, "moisture", sizeof(event.trigger) - 1);
    strncpy(event.result, "completed", sizeof(event.result) - 1);
    wateringEventBufferPush(events, event);
    char payload[CLOUD_PAYLOAD_MAX_LEN];
    TEST_ASSERT_TRUE(buildIngestPayload(payload, sizeof(payload), "zone_1", readings, events,
                                        -62, 3.98f, 100, 1010) > 0);
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"moisture_pct\":42.5"));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"request_id\":\"local_1\""));
    char tiny[8];
    TEST_ASSERT_EQUAL(0, buildIngestPayload(tiny, sizeof(tiny), "zone_1", readings, events,
                                            -62, 3.98f, 100, 1010));
}

void test_maximum_batch_fits(void) {
    ReadingBuffer readings = {};
    for (size_t i = 0; i < READING_BUFFER_CAPACITY; ++i) {
        readingBufferPush(readings, 42.5f, 1753277940 + static_cast<uint32_t>(i));
    }
    WateringEventBuffer events = {};
    WateringEventRecord event = {};
    strncpy(event.requestId, "12345678-1234-1234-1234-123456789012", sizeof(event.requestId) - 1);
    strncpy(event.trigger, "moisture", sizeof(event.trigger) - 1);
    strncpy(event.result, "completed", sizeof(event.result) - 1);
    for (size_t i = 0; i < WATERING_EVENT_BUFFER_CAPACITY; ++i) wateringEventBufferPush(events, event);
    char payload[CLOUD_PAYLOAD_MAX_LEN];
    TEST_ASSERT_TRUE(buildIngestPayload(payload, sizeof(payload), "zone_1", readings, events,
                                        -62, 3.98f, 100, 1753279000) > 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cloud_payload);
    RUN_TEST(test_maximum_batch_fits);
    return UNITY_END();
}
