#include <unity.h>
#include "reading_buffer.h"

void setUp(void) {}
void tearDown(void) {}

void test_reading_buffer(void) {
    ReadingBuffer buffer = {};
    for (size_t i = 0; i <= READING_BUFFER_CAPACITY; ++i) {
        readingBufferPush(buffer, static_cast<float>(i), static_cast<uint32_t>(i));
    }
    TEST_ASSERT_EQUAL(READING_BUFFER_CAPACITY, buffer.count);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, buffer.moisturePct[0]);
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(READING_BUFFER_CAPACITY), buffer.moisturePct[READING_BUFFER_CAPACITY - 1]);
    readingBufferClear(buffer);
    TEST_ASSERT_EQUAL(0, buffer.count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reading_buffer);
    return UNITY_END();
}
