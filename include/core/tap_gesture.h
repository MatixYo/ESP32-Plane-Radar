#pragma once

namespace core::gesture {

enum class Tap {
  kNone,
  kSingle,
  kDouble,
};

/** Record a BOOT tap at now_ms (from bootButtonConsumeTap()). */
void tapPress(unsigned long now_ms);

/** Resolve pending taps once the double-tap window expires. */
Tap tapPoll(unsigned long now_ms);

/** Reset classifier state (unit tests). */
void tapReset();

}  // namespace core::gesture
