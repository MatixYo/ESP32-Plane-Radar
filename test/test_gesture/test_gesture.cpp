/**
 * Host unit tests for the BOOT tap single/double classifier.
 */

#include <unity.h>

#include "config.h"
#include "core/tap_gesture.h"

namespace cg = core::gesture;

void test_single_tap_after_window(void) {
  cg::tapReset();
  cg::tapPress(1000);
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(1200));
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(1499));
  TEST_ASSERT_EQUAL(cg::Tap::kSingle, cg::tapPoll(1500));
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(1500));
}

void test_double_tap_within_window(void) {
  cg::tapReset();
  cg::tapPress(1000);
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(1100));
  cg::tapPress(1200);
  TEST_ASSERT_EQUAL(cg::Tap::kDouble, cg::tapPoll(1200));
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(1500));
}

void test_two_singles_not_double(void) {
  cg::tapReset();
  cg::tapPress(1000);
  TEST_ASSERT_EQUAL(cg::Tap::kSingle, cg::tapPoll(1000 + config::kDoubleTapWindowMs));
  cg::tapPress(2000);
  TEST_ASSERT_EQUAL(cg::Tap::kSingle, cg::tapPoll(2000 + config::kDoubleTapWindowMs));
}

/** Real press times: second tap inside the window must be kDouble even if polled late. */
void test_double_tap_real_timestamps_late_poll(void) {
  cg::tapReset();
  cg::tapPress(1000);
  cg::tapPress(1200);
  TEST_ASSERT_EQUAL(cg::Tap::kDouble, cg::tapPoll(5000));
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(5000));
}

/** Real press times: taps farther apart than the window are two singles, not one double. */
void test_two_singles_real_timestamps(void) {
  cg::tapReset();
  cg::tapPress(1000);
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(1100));
  TEST_ASSERT_EQUAL(cg::Tap::kSingle, cg::tapPoll(1000 + config::kDoubleTapWindowMs));
  cg::tapPress(3000);
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(3100));
  TEST_ASSERT_EQUAL(cg::Tap::kSingle, cg::tapPoll(3000 + config::kDoubleTapWindowMs));
}

/**
 * The drain loop's exact order: resolve at the new tap's timestamp, then
 * record it. Without the first tapPoll, two taps seconds apart delivered in one
 * drain would collapse into a single gesture — there is no poll in between when
 * the loop was blocked for the whole interval.
 */
void test_drain_of_two_far_apart_taps_yields_two_singles(void) {
  cg::tapReset();
  TEST_ASSERT_EQUAL(cg::Tap::kNone, cg::tapPoll(1000));
  cg::tapPress(1000);
  TEST_ASSERT_EQUAL(cg::Tap::kSingle, cg::tapPoll(3000));
  cg::tapPress(3000);
  TEST_ASSERT_EQUAL(cg::Tap::kSingle,
                    cg::tapPoll(3000 + config::kDoubleTapWindowMs));
}

void setUp(void) { cg::tapReset(); }
void tearDown(void) { cg::tapReset(); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_tap_after_window);
  RUN_TEST(test_double_tap_within_window);
  RUN_TEST(test_two_singles_not_double);
  RUN_TEST(test_double_tap_real_timestamps_late_poll);
  RUN_TEST(test_two_singles_real_timestamps);
  RUN_TEST(test_drain_of_two_far_apart_taps_yields_two_singles);
  return UNITY_END();
}
