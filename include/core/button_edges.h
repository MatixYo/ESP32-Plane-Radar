#pragma once

/**
 * Debounce and hold classification for one active-low button.
 *
 * Pure state: no GPIO, no clock, no logging — the caller samples the pin and
 * supplies the timestamp. That is what lets the device ISR, the native SDL poll
 * and the host unit tests all drive the same classifier, and it is why this
 * lives in core/ (the only layer env:native_test compiles).
 *
 * Taps are queued with the millis() of their release edge rather than being
 * latched as a flag, because the consumer cannot run promptly. A blocking
 * HttpClient::get leaves DNS, TCP and the TLS handshake unpolled for a second
 * or more, and an NVS commit masks the GPIO interrupt entirely (the Arduino
 * core builds this target with CONFIG_ARDUINO_ISR_IRAM unset, so the edge is
 * latched in GPIO_STATUS and serviced late). Consume-time timestamps turned
 * both of those into misclassified gestures: a double tap read as a single, or
 * as two singles seconds apart.
 */

#include <cstdint>

namespace core::button {

constexpr uint8_t kTapQueueLen = 4;

struct Tracker {
  bool down = false;
  unsigned long down_ms = 0;
  bool long_reported = false;  ///< long press already raised for this press
  bool long_pending = false;   ///< one-shot, consumed by popLongPress
  unsigned long taps[kTapQueueLen] = {};
  uint8_t head = 0;
  uint8_t tail = 0;
};

/**
 * Feed one sample of the pin level (true = pressed).
 *
 * Concurrent callers are the caller's problem: the device holds s_boot_mux
 * across both its ISR and its poll, the native harness is single-threaded.
 *
 * Queues the release timestamp when a press lasted at least
 * config::kBootTapMinMs and less than config::kBootResetHoldMs. Raises
 * long_pending exactly once per press — while still held once
 * kBootResetHoldMs is reached, or at the release edge if no sample landed
 * during the hold, which is the case when the whole hold falls inside an
 * unpolled window. Drops the newest tap when the queue is full.
 */
void sample(Tracker& t, bool down, unsigned long now_ms);

/** Pop the oldest queued tap; *tap_ms is its release time. FIFO. */
bool popTap(Tracker& t, unsigned long* tap_ms);

/** Consume the one-shot long-press flag. */
bool popLongPress(Tracker& t);

void reset(Tracker& t);

}  // namespace core::button
