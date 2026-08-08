#include <unity.h>
#include "hal/clock.h"

void setUp(void) {}
void tearDown(void) {}

void test_local_hour_utc_matches_simple_conversion(void) {
    initClockTimezone("UTC0");
    const int hour = currentLocalHour();
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, hour);
    TEST_ASSERT_LESS_THAN_INT(24, hour);
}

void test_timezone_offset_applies_to_local_hour(void) {
    // UTC+5: local time should be 5 hours ahead of UTC for the same instant.
    initClockTimezone("UTC-5");
    const int localHour = currentLocalHour();

    initClockTimezone("UTC0");
    const int utcHour = currentLocalHour();

    // Hours wrap, so compare modulo 24.
    int expected = (utcHour + 5) % 24;
    TEST_ASSERT_EQUAL_INT(expected, localHour);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_local_hour_utc_matches_simple_conversion);
    RUN_TEST(test_timezone_offset_applies_to_local_hour);
    return UNITY_END();
}
