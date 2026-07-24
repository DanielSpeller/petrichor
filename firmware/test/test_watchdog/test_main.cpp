#include <unity.h>
#include "watchdog.h"
#include "hal/pump_relay.h"

void setUp(void) {}
void tearDown(void) {}

void test_enforce_safe_default_turns_pump_off(void) {
    setPumpRelay(true); // simulate pump left running from some prior state
    enforceSafeDefaultPumpOff();
    TEST_ASSERT_FALSE(getPumpRelayState());
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_enforce_safe_default_turns_pump_off);
    return UNITY_END();
}
