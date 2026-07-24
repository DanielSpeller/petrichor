#include <unity.h>
#include "wifi_manager.h"

void setUp(void) {
    setMockWifiShouldConnect(true); // reset to default before each test
}
void tearDown(void) {}

void test_connects_immediately_when_mock_wifi_available(void) {
    WifiManager manager(1000, 30000);
    manager.update(0);
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::CONNECTED), static_cast<int>(manager.state()));
}

void test_exponential_backoff_grows_after_failed_attempts(void) {
    setMockWifiShouldConnect(false);
    WifiManager manager(1000, 30000);

    manager.update(0);      // attempt #1 fails at t=0; next attempt at t=1000
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::DISCONNECTED), static_cast<int>(manager.state()));

    manager.update(500);    // too soon, must not attempt again yet
    manager.update(1000);   // attempt #2 fails at t=1000; backoff doubles to 2000ms, next at 3000
    manager.update(2999);   // too soon
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::DISCONNECTED), static_cast<int>(manager.state()));

    setMockWifiShouldConnect(true);
    manager.update(3000);   // attempt #3 at t=3000 succeeds
    TEST_ASSERT_EQUAL(static_cast<int>(WifiState::CONNECTED), static_cast<int>(manager.state()));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_connects_immediately_when_mock_wifi_available);
    RUN_TEST(test_exponential_backoff_grows_after_failed_attempts);
    return UNITY_END();
}
