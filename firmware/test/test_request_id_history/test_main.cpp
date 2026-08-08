#include <cstdio>
#include <unity.h>
#include "request_id_history.h"

void setUp(void) {}
void tearDown(void) {}

void test_new_request_id_is_not_duplicate(void) {
    RequestIdHistory history;
    TEST_ASSERT_FALSE(history.isDuplicate("req-1"));
}

void test_same_request_id_is_duplicate(void) {
    RequestIdHistory history;
    TEST_ASSERT_FALSE(history.isDuplicate("req-1"));
    TEST_ASSERT_TRUE(history.isDuplicate("req-1"));
}

void test_different_request_ids_are_not_duplicates(void) {
    RequestIdHistory history;
    TEST_ASSERT_FALSE(history.isDuplicate("req-1"));
    TEST_ASSERT_FALSE(history.isDuplicate("req-2"));
    TEST_ASSERT_TRUE(history.isDuplicate("req-1"));
}

void test_history_wraps_and_forgets_oldest(void) {
    RequestIdHistory history;
    for (int i = 0; i < static_cast<int>(RequestIdHistory::HISTORY_SIZE) + 1; ++i) {
        char buf[16];
        snprintf(buf, sizeof(buf), "req-%d", i);
        TEST_ASSERT_FALSE(history.isDuplicate(buf));
    }
    // req-1 is still in the buffer (check it before probing req-0, because a miss
    // inserts the id and would overwrite an entry).
    TEST_ASSERT_TRUE(history.isDuplicate("req-1"));
    // The oldest entry (req-0) was evicted when req-size was inserted at its slot.
    TEST_ASSERT_FALSE(history.isDuplicate("req-0"));
}

void test_empty_request_id_is_not_duplicate(void) {
    RequestIdHistory history;
    TEST_ASSERT_FALSE(history.isDuplicate(""));
    TEST_ASSERT_FALSE(history.isDuplicate(""));
}

void test_clear_removes_all_entries(void) {
    RequestIdHistory history;
    history.isDuplicate("req-1");
    TEST_ASSERT_TRUE(history.isDuplicate("req-1"));
    history.clear();
    TEST_ASSERT_FALSE(history.isDuplicate("req-1"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_new_request_id_is_not_duplicate);
    RUN_TEST(test_same_request_id_is_duplicate);
    RUN_TEST(test_different_request_ids_are_not_duplicates);
    RUN_TEST(test_history_wraps_and_forgets_oldest);
    RUN_TEST(test_empty_request_id_is_not_duplicate);
    RUN_TEST(test_clear_removes_all_entries);
    return UNITY_END();
}
