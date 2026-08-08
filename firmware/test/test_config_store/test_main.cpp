#include <unity.h>
#include "config_store.h"

void setUp(void) {}
void tearDown(void) {}

void test_defaults_match_compile_time_constants(void) {
    ConfigStore store;
    store.load();
    TEST_ASSERT_EQUAL_FLOAT(30.0f, store.moistureThresholdPct());
    TEST_ASSERT_EQUAL_FLOAT(5.0f, store.moistureHysteresisPct());
    TEST_ASSERT_EQUAL_UINT32(10, store.minWateringDurationSec());
    TEST_ASSERT_EQUAL_UINT32(900, store.cooldownPeriodSec());
    TEST_ASSERT_EQUAL_INT(6, store.scheduleWindowStartHour());
    TEST_ASSERT_EQUAL_INT(20, store.scheduleWindowEndHour());
    TEST_ASSERT_EQUAL_INT(4, store.maxWateringsPerDay());
}

void test_setters_override_defaults(void) {
    ConfigStore store;
    store.load();

    store.setMoistureThresholdPct(25.0f);
    store.setMoistureHysteresisPct(3.0f);
    store.setMinWateringDurationSec(5);
    store.setCooldownPeriodSec(600);
    store.setScheduleWindowStartHour(7);
    store.setScheduleWindowEndHour(21);
    store.setMaxWateringsPerDay(3);

    TEST_ASSERT_EQUAL_FLOAT(25.0f, store.moistureThresholdPct());
    TEST_ASSERT_EQUAL_FLOAT(3.0f, store.moistureHysteresisPct());
    TEST_ASSERT_EQUAL_UINT32(5, store.minWateringDurationSec());
    TEST_ASSERT_EQUAL_UINT32(600, store.cooldownPeriodSec());
    TEST_ASSERT_EQUAL_INT(7, store.scheduleWindowStartHour());
    TEST_ASSERT_EQUAL_INT(21, store.scheduleWindowEndHour());
    TEST_ASSERT_EQUAL_INT(3, store.maxWateringsPerDay());
}

void test_reset_to_defaults_restores_compile_time_values(void) {
    ConfigStore store;
    store.load();
    store.setMoistureThresholdPct(99.0f);
    store.resetToDefaults();
    TEST_ASSERT_EQUAL_FLOAT(30.0f, store.moistureThresholdPct());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_defaults_match_compile_time_constants);
    RUN_TEST(test_setters_override_defaults);
    RUN_TEST(test_reset_to_defaults_restores_compile_time_values);
    return UNITY_END();
}
