#include "core/button_edges.h"

#include "config.h"

namespace core::button {

namespace {

/**
 * head and tail are free-running cursors, not wrapped indices: with only two
 * uint8_t fields in the struct, a full queue would otherwise be
 * indistinguishable from an empty one and cost a slot. kTapQueueLen divides
 * 256, so the natural uint8_t wrap keeps both this difference and the modulo
 * index below exact.
 */
uint8_t queuedTaps(const Tracker& t) {
  return static_cast<uint8_t>(t.head - t.tail);
}

void queueTap(Tracker& t, unsigned long release_ms) {
  if (queuedTaps(t) >= kTapQueueLen) {
    // A full queue means the consumer has been blocked for seconds. The
    // gestures still waiting to be resolved are the older ones, so the newest
    // tap is the one to lose.
    return;
  }
  t.taps[t.head % kTapQueueLen] = release_ms;
  t.head = static_cast<uint8_t>(t.head + 1);
}

void raiseLongPress(Tracker& t) {
  t.long_reported = true;
  t.long_pending = true;
}

}  // namespace

void sample(Tracker& t, bool down, unsigned long now_ms) {
  // Every comparison is now - then, never then + interval: millis() wraps
  // every 49.7 days on the device and unsigned subtraction survives that.
  if (down) {
    if (!t.down) {
      t.down = true;
      t.down_ms = now_ms;
      t.long_reported = false;
    }
    if (!t.long_reported && now_ms - t.down_ms >= config::kBootResetHoldMs) {
      raiseLongPress(t);
    }
    return;
  }

  if (!t.down) {
    return;
  }
  t.down = false;
  if (t.long_reported) {
    return;
  }

  // Reached either on the real release edge or on the first sample after a
  // release edge that was never delivered; in the latter case now_ms is the
  // latest the button can have come up, and so the best estimate available.
  const unsigned long held = now_ms - t.down_ms;
  if (held >= config::kBootResetHoldMs) {
    // No sample landed during the hold, so the threshold is only observable
    // here. Same press, so still exactly one long_pending.
    raiseLongPress(t);
  } else if (held >= config::kBootTapMinMs) {
    queueTap(t, now_ms);
  }
  // Anything shorter is contact bounce, on either edge.
}

bool popTap(Tracker& t, unsigned long* tap_ms) {
  if (t.head == t.tail) {
    return false;
  }
  if (tap_ms != nullptr) {
    *tap_ms = t.taps[t.tail % kTapQueueLen];
  }
  t.tail = static_cast<uint8_t>(t.tail + 1);
  return true;
}

bool popLongPress(Tracker& t) {
  const bool pending = t.long_pending;
  t.long_pending = false;
  return pending;
}

void reset(Tracker& t) { t = Tracker{}; }

}  // namespace core::button
