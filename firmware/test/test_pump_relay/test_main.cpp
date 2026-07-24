#include <unity.h>
#include "hal/pump_relay.h"

void setUp(void) {}
void tearDown(void) {}

void test_relay_defaults_to_off(void) {
    TEST_ASSERT_FALSE(getPumpRelayState());
}

void test_relay_reflects_last_set_state(void) {
    setPumpRelay(true);
    TEST_ASSERT_TRUE(getPumpRelayState());

    setPumpRelay(false);
    TEST_ASSERT_FALSE(getPumpRelayState());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_relay_defaults_to_off);
    RUN_TEST(test_relay_reflects_last_set_state);
    return UNITY_END();
}
