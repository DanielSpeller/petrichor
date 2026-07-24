#include <unity.h>
#include "watering_controller.h"

void setUp(void) {}
void tearDown(void) {}

void test_dry_soil_triggers_watering(void) {
    WateringController controller(30.0f, 5.0f, 900);
    WaterDecision decision = controller.evaluate(20.0f, 1000);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED), static_cast<int>(decision));
}

void test_hysteresis_prevents_flapping_near_threshold(void) {
    WateringController controller(30.0f, 5.0f, 900);

    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED),
                       static_cast<int>(controller.evaluate(20.0f, 1000)));
    controller.notifyWateringComplete(1000);

    // Cooldown has fully elapsed, but moisture only recovered to 29% --
    // below the 35% rearm threshold -- so it must not retrigger.
    WaterDecision afterCooldown = controller.evaluate(29.0f, 1000 + 900 + 1);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_HYSTERESIS_LOCKOUT),
                       static_cast<int>(afterCooldown));

    // Moisture genuinely recovers past the rearm threshold (35%).
    WaterDecision rearmed = controller.evaluate(36.0f, 1000 + 900 + 100);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_ABOVE_THRESHOLD),
                       static_cast<int>(rearmed));

    // Now it dries out again -- should be able to trigger.
    WaterDecision retrigger = controller.evaluate(25.0f, 1000 + 900 + 200);
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED),
                       static_cast<int>(retrigger));
}

void test_cooldown_blocks_second_trigger(void) {
    WateringController controller(30.0f, 5.0f, 900);

    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::WATER_TRIGGERED),
                       static_cast<int>(controller.evaluate(20.0f, 1000)));
    controller.notifyWateringComplete(1000);

    // Moisture spikes well past rearm (soil got wet), then dries out again
    // quickly, inside the 900s cooldown window.
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_ABOVE_THRESHOLD),
                       static_cast<int>(controller.evaluate(80.0f, 1010)));

    WaterDecision withinCooldown = controller.evaluate(20.0f, 1200); // 200s after watering ended
    TEST_ASSERT_EQUAL(static_cast<int>(WaterDecision::NO_WATER_COOLDOWN),
                       static_cast<int>(withinCooldown));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_dry_soil_triggers_watering);
    RUN_TEST(test_hysteresis_prevents_flapping_near_threshold);
    RUN_TEST(test_cooldown_blocks_second_trigger);
    return UNITY_END();
}
