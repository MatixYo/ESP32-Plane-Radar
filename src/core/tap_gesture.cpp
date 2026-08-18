#include "core/tap_gesture.h"

#include "config.h"

namespace core::gesture {

namespace {

unsigned long s_first_tap_ms = 0;
bool s_have_pending = false;
Tap s_ready = Tap::kNone;

}  // namespace

void tapPress(unsigned long now_ms) {
  if (s_have_pending &&
      (now_ms - s_first_tap_ms) < config::kDoubleTapWindowMs) {
    s_have_pending = false;
    s_ready = Tap::kDouble;
    return;
  }

  s_first_tap_ms = now_ms;
  s_have_pending = true;
}

Tap tapPoll(unsigned long now_ms) {
  if (s_ready != Tap::kNone) {
    const Tap result = s_ready;
    s_ready = Tap::kNone;
    return result;
  }

  if (s_have_pending &&
      (now_ms - s_first_tap_ms) >= config::kDoubleTapWindowMs) {
    s_have_pending = false;
    return Tap::kSingle;
  }

  return Tap::kNone;
}

void tapReset() {
  s_first_tap_ms = 0;
  s_have_pending = false;
  s_ready = Tap::kNone;
}

}  // namespace core::gesture
