#include <unity.h>
#include "sync_scheduler.h"

void setUp(void) {}
void tearDown(void) {}

void test_sync_scheduler(void) {
    uint32_t counter = 0;
    TEST_ASSERT_FALSE(syncDueThisWake(counter, 3));
    TEST_ASSERT_FALSE(syncDueThisWake(counter, 3));
    TEST_ASSERT_TRUE(syncDueThisWake(counter, 3));
    TEST_ASSERT_EQUAL_UINT32(0, counter);
    TEST_ASSERT_TRUE(syncDueThisWake(counter, 1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sync_scheduler);
    return UNITY_END();
}
