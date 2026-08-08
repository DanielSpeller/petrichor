#include <unity.h>

#include "pump_runner.h"
#include "hal/pump_relay.h"

void setUp(void) {
    setPumpRelay(false);
}

void tearDown(void) {}

void test_pump_starts_and_runs_for_requested_duration(void) {
    PumpRunner runner;
    TEST_ASSERT_FALSE(runner.isRunning());

    TEST_ASSERT_TRUE(runner.start(1000, 100, 2, "req-123", "moisture", 25.0f));
    TEST_ASSERT_TRUE(runner.isRunning());
    TEST_ASSERT_TRUE(getPumpRelayState());

    // Before duration elapses, update returns NONE.
    PumpRunner::Result result;
    TEST_ASSERT_EQUAL(static_cast<int>(PumpRunResult::NONE),
                      static_cast<int>(runner.update(2000, 102, result)));
    TEST_ASSERT_TRUE(runner.isRunning());

    // At exactly 2s elapsed the run completes.
    PumpRunResult runResult = runner.update(3000, 103, result);
    TEST_ASSERT_EQUAL(static_cast<int>(PumpRunResult::COMPLETED),
                      static_cast<int>(runResult));
    TEST_ASSERT_FALSE(runner.isRunning());
    TEST_ASSERT_FALSE(getPumpRelayState());
    TEST_ASSERT_EQUAL_STRING("req-123", result.requestId);
    TEST_ASSERT_EQUAL_STRING("moisture", result.trigger);
    TEST_ASSERT_EQUAL(2, result.requestedDurationSec);
    TEST_ASSERT_EQUAL(2, result.actualDurationSec);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, result.moistureBeforePct);
    TEST_ASSERT_EQUAL(103, result.completedAtSec);
}

void test_stop_ends_run_early(void) {
    PumpRunner runner;
    TEST_ASSERT_TRUE(runner.start(1000, 100, 10, "req-early", "manual", 30.0f));

    PumpRunner::Result result;
    PumpRunResult runResult = runner.stop(1500, 101, result);

    TEST_ASSERT_EQUAL(static_cast<int>(PumpRunResult::STOPPED_EARLY),
                      static_cast<int>(runResult));
    TEST_ASSERT_FALSE(runner.isRunning());
    TEST_ASSERT_FALSE(getPumpRelayState());
    TEST_ASSERT_EQUAL_STRING("req-early", result.requestId);
    TEST_ASSERT_EQUAL_STRING("manual", result.trigger);
    TEST_ASSERT_EQUAL(10, result.requestedDurationSec);
    TEST_ASSERT_EQUAL(0, result.actualDurationSec);
    TEST_ASSERT_EQUAL(101, result.completedAtSec);
}

void test_second_start_while_running_fails(void) {
    PumpRunner runner;
    TEST_ASSERT_TRUE(runner.start(1000, 100, 5, "req-first", "schedule", 20.0f));
    TEST_ASSERT_FALSE(runner.start(2000, 101, 5, "req-second", "schedule", 20.0f));

    PumpRunner::Result result;
    runner.stop(3000, 102, result);
    TEST_ASSERT_EQUAL_STRING("req-first", result.requestId);
}

void test_zero_duration_rejected(void) {
    PumpRunner runner;
    TEST_ASSERT_FALSE(runner.start(1000, 100, 0, "req-zero", "moisture", 20.0f));
    TEST_ASSERT_FALSE(runner.isRunning());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_pump_starts_and_runs_for_requested_duration);
    RUN_TEST(test_stop_ends_run_early);
    RUN_TEST(test_second_start_while_running_fails);
    RUN_TEST(test_zero_duration_rejected);
    return UNITY_END();
}
