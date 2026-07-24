#include <unity.h>
#include "schedule.h"
#include "watering_controller.h"

void setUp(void) {}
void tearDown(void) {}

void test_within_window_and_under_limit_allows_watering(void) {
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    TEST_ASSERT_TRUE(schedule.allowsWatering(10, 1));
}

void test_outside_window_blocks_watering(void) {
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    TEST_ASSERT_FALSE(schedule.allowsWatering(22, 0));
}

void test_at_daily_limit_blocks_watering(void) {
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    TEST_ASSERT_FALSE(schedule.allowsWatering(10, 4));
}

void test_schedule_blocks_moisture_triggered_water_outside_window(void) {
    // The moisture controller wants to water (soil is dry)...
    WateringController controller(30.0f, 5.0f, 900);
    WaterDecision decision = controller.evaluate(20.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED), static_cast<int>(decision));

    // ...but it's outside the allowed window, so the schedule vetoes it.
    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    bool allowed = schedule.allowsWatering(23, 0); // 11pm, outside 06:00-20:00
    TEST_ASSERT_FALSE(allowed);

    // main.cpp's rule: only actually command a watering if both the
    // moisture controller AND the schedule agree.
    bool shouldWaterNow = (decision == WaterDecision::WATER_TRIGGERED) && allowed;
    TEST_ASSERT_FALSE(shouldWaterNow);
}

void test_schedule_alone_never_forces_a_watering(void) {
    // Moisture is fine (not dry) -- even though the schedule window is
    // open and under the daily limit, nothing should trigger a watering.
    // The schedule can only veto a moisture trigger, never create one.
    WateringController controller(30.0f, 5.0f, 900);
    WaterDecision decision = controller.evaluate(60.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_ABOVE_THRESHOLD), static_cast<int>(decision));

    ScheduleConfig config{6, 20, 4};
    Schedule schedule(config);
    bool allowed = schedule.allowsWatering(10, 0); // well within window

    bool shouldWaterNow = (decision == WaterDecision::WATER_TRIGGERED) && allowed;
    TEST_ASSERT_FALSE(shouldWaterNow); // allowed=true doesn't matter; decision wasn't a trigger
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_within_window_and_under_limit_allows_watering);
    RUN_TEST(test_outside_window_blocks_watering);
    RUN_TEST(test_at_daily_limit_blocks_watering);
    RUN_TEST(test_schedule_blocks_moisture_triggered_water_outside_window);
    RUN_TEST(test_schedule_alone_never_forces_a_watering);
    return UNITY_END();
}
