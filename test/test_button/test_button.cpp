/**
 * Host unit tests for core::button — the BOOT button edge classifier.
 *
 * This logic used to exist twice, once in a GPIO ISR and once in the native
 * poll loop, and both copies were only observable by pressing a physical or
 * emulated button. The cases that actually bit are the ones no manual test
 * reproduces on purpose: an edge delivered late, a release edge lost entirely,
 * and several taps queued while the main loop sat in a blocking fetch.
 */

#include <unity.h>

#include <climits>

#include "config.h"
#include "core/button_edges.h"

namespace cb = core::button;

namespace {

cb::Tracker t;

void assertNextTap(unsigned long expected_ms) {
  unsigned long tap_ms = 0;
  TEST_ASSERT_TRUE(cb::popTap(t, &tap_ms));
  TEST_ASSERT_EQUAL_UINT32(expected_ms, tap_ms);
}

void assertNoMoreTaps() {
  unsigned long tap_ms = 0;
  TEST_ASSERT_FALSE(cb::popTap(t, &tap_ms));
}

}  // namespace

// --- debounce ----------------------------------------------------------------

void test_samples_while_idle_do_nothing(void) {
  // The device polls this on every loop iteration with the button up.
  cb::sample(t, false, 1000);
  cb::sample(t, false, 1010);
  cb::sample(t, false, 9000);
  assertNoMoreTaps();
  TEST_ASSERT_FALSE(cb::popLongPress(t));
}

void test_press_shorter_than_the_debounce_is_ignored(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, false, 1000 + config::kBootTapMinMs - 1);
  assertNoMoreTaps();
  TEST_ASSERT_FALSE(cb::popLongPress(t));
}

void test_press_at_the_debounce_boundary_is_a_tap(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, false, 1000 + config::kBootTapMinMs);
  assertNextTap(1000 + config::kBootTapMinMs);
  assertNoMoreTaps();
}

// --- taps --------------------------------------------------------------------

void test_tap_queues_its_release_timestamp(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, false, 1200);
  assertNextTap(1200);
  assertNoMoreTaps();
  TEST_ASSERT_FALSE(cb::popLongPress(t));
}

void test_press_bounce_yields_one_tap(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, false, 1005);  // contact bounce on the way down
  cb::sample(t, true, 1012);
  cb::sample(t, false, 1200);
  assertNextTap(1200);
  assertNoMoreTaps();
}

void test_release_bounce_yields_one_tap(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, false, 1200);
  cb::sample(t, true, 1205);  // contact bounce on the way up
  cb::sample(t, false, 1210);
  assertNextTap(1200);
  assertNoMoreTaps();
}

void test_missed_release_edge_is_recovered_by_a_later_sample(void) {
  // An NVS commit masks the GPIO interrupt, so the release edge can be lost
  // outright. The next poll that sees the pin high still owes us the tap, with
  // the poll's own timestamp as the best estimate of the release.
  cb::sample(t, true, 1000);
  cb::sample(t, true, 1050);
  cb::sample(t, false, 1300);
  assertNextTap(1300);
  assertNoMoreTaps();
  TEST_ASSERT_FALSE(cb::popLongPress(t));
}

// --- holds -------------------------------------------------------------------

void test_hold_sampled_during_the_press_raises_one_long_press(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, true, 2000);
  TEST_ASSERT_FALSE(cb::popLongPress(t));
  cb::sample(t, true, 1000 + config::kBootResetHoldMs);
  TEST_ASSERT_TRUE(cb::popLongPress(t));

  // Once per press, not once per poll: the reset must not fire twice.
  cb::sample(t, true, 5000);
  cb::sample(t, false, 6000);
  TEST_ASSERT_FALSE(cb::popLongPress(t));
  assertNoMoreTaps();
}

void test_hold_seen_only_at_its_edges_still_raises_a_long_press(void) {
  // The whole hold fell inside a blocking fetch, so no sample landed between
  // the two edges and the threshold is only observable at the release.
  cb::sample(t, true, 1000);
  cb::sample(t, false, 4500);
  TEST_ASSERT_TRUE(cb::popLongPress(t));
  TEST_ASSERT_FALSE(cb::popLongPress(t));
  assertNoMoreTaps();
}

void test_hold_at_the_reset_threshold_is_not_also_a_tap(void) {
  // The tap window is half-open: [kBootTapMinMs, kBootResetHoldMs).
  cb::sample(t, true, 1000);
  cb::sample(t, false, 1000 + config::kBootResetHoldMs);
  TEST_ASSERT_TRUE(cb::popLongPress(t));
  assertNoMoreTaps();
}

void test_a_tap_after_a_hold_is_classified_normally(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, false, 4500);
  TEST_ASSERT_TRUE(cb::popLongPress(t));

  cb::sample(t, true, 5000);
  cb::sample(t, false, 5200);
  assertNextTap(5200);
  TEST_ASSERT_FALSE(cb::popLongPress(t));
}

// --- queue -------------------------------------------------------------------

void test_queue_overflow_drops_the_newest_tap(void) {
  const unsigned long releases[] = {1100, 1300, 1500, 1700, 1900};
  for (unsigned long release_ms : releases) {
    cb::sample(t, true, release_ms - 100);
    cb::sample(t, false, release_ms);
  }

  // kTapQueueLen taps survive, in the order they were pressed.
  assertNextTap(1100);
  assertNextTap(1300);
  assertNextTap(1500);
  assertNextTap(1700);
  assertNoMoreTaps();
}

void test_fifo_order_survives_the_cursor_wrap(void) {
  // Enough rounds to push more than 256 taps, so the queue cursors wrap as
  // uint8_t as well as past the end of the taps array. Both are where a
  // mis-indexed ring starts returning stale timestamps or reporting empty.
  for (int round = 0; round < 66; ++round) {
    const unsigned long base = 1000 + static_cast<unsigned long>(round) * 1000;
    for (int i = 0; i < cb::kTapQueueLen; ++i) {
      const unsigned long press_ms = base + static_cast<unsigned long>(i) * 100;
      cb::sample(t, true, press_ms);
      cb::sample(t, false, press_ms + 50);
    }
    for (int i = 0; i < cb::kTapQueueLen; ++i) {
      assertNextTap(base + static_cast<unsigned long>(i) * 100 + 50);
    }
    assertNoMoreTaps();
  }
}

void test_reset_clears_queued_taps_and_a_pending_long_press(void) {
  cb::sample(t, true, 1000);
  cb::sample(t, false, 1200);
  cb::sample(t, true, 1300);
  cb::sample(t, false, 4800);

  cb::reset(t);
  assertNoMoreTaps();
  TEST_ASSERT_FALSE(cb::popLongPress(t));

  cb::sample(t, true, 5000);
  cb::sample(t, false, 5200);
  assertNextTap(5200);
}

// --- millis() rollover -------------------------------------------------------

void test_a_tap_across_the_millis_rollover_is_still_a_tap(void) {
  const unsigned long before_wrap = ULONG_MAX - 20UL;
  cb::sample(t, true, before_wrap);
  cb::sample(t, false, 30UL);  // 51 ms held, spanning the wrap
  assertNextTap(30UL);
  TEST_ASSERT_FALSE(cb::popLongPress(t));
}

void test_bounce_across_the_millis_rollover_is_still_bounce(void) {
  const unsigned long before_wrap = ULONG_MAX - 20UL;
  cb::sample(t, true, before_wrap);
  cb::sample(t, false, 5UL);  // 26 ms held
  assertNoMoreTaps();
  TEST_ASSERT_FALSE(cb::popLongPress(t));
}

void setUp(void) { cb::reset(t); }
void tearDown(void) { cb::reset(t); }

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_samples_while_idle_do_nothing);
  RUN_TEST(test_press_shorter_than_the_debounce_is_ignored);
  RUN_TEST(test_press_at_the_debounce_boundary_is_a_tap);

  RUN_TEST(test_tap_queues_its_release_timestamp);
  RUN_TEST(test_press_bounce_yields_one_tap);
  RUN_TEST(test_release_bounce_yields_one_tap);
  RUN_TEST(test_missed_release_edge_is_recovered_by_a_later_sample);

  RUN_TEST(test_hold_sampled_during_the_press_raises_one_long_press);
  RUN_TEST(test_hold_seen_only_at_its_edges_still_raises_a_long_press);
  RUN_TEST(test_hold_at_the_reset_threshold_is_not_also_a_tap);
  RUN_TEST(test_a_tap_after_a_hold_is_classified_normally);

  RUN_TEST(test_queue_overflow_drops_the_newest_tap);
  RUN_TEST(test_fifo_order_survives_the_cursor_wrap);
  RUN_TEST(test_reset_clears_queued_taps_and_a_pending_long_press);

  RUN_TEST(test_a_tap_across_the_millis_rollover_is_still_a_tap);
  RUN_TEST(test_bounce_across_the_millis_rollover_is_still_bounce);

  return UNITY_END();
}
