#include <unity.h>
#include "hal/moisture_sensor.h"

void setUp(void) {}
void tearDown(void) {}

void test_scripted_sequence_returns_values_in_order(void) {
    float sequence[] = {10.0f, 55.0f, 90.0f};
    setScriptedMoistureSequence(sequence, 3);

    TEST_ASSERT_EQUAL_FLOAT(10.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(55.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(90.0f, readMoisturePercent());
}

void test_scripted_sequence_repeats_last_value_once_exhausted(void) {
    float sequence[] = {42.0f};
    setScriptedMoistureSequence(sequence, 1);

    TEST_ASSERT_EQUAL_FLOAT(42.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(42.0f, readMoisturePercent());
    TEST_ASSERT_EQUAL_FLOAT(42.0f, readMoisturePercent());
}

void test_randomized_mock_stays_within_0_to_100(void) {
    setScriptedMoistureSequence(nullptr, 0); // revert to randomized mock
    for (int i = 0; i < 50; i++) {
        float value = readMoisturePercent();
        TEST_ASSERT_TRUE(value >= 0.0f && value <= 100.0f);
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_scripted_sequence_returns_values_in_order);
    RUN_TEST(test_scripted_sequence_repeats_last_value_once_exhausted);
    RUN_TEST(test_randomized_mock_stays_within_0_to_100);
    return UNITY_END();
}
