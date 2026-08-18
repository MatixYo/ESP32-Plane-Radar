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

void setUp(void) { cg::tapReset(); }
void tearDown(void) { cg::tapReset(); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_tap_after_window);
  RUN_TEST(test_double_tap_within_window);
  RUN_TEST(test_two_singles_not_double);
  return UNITY_END();
}
