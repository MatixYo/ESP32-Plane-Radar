/**
 * Native BOOT button: a keyboard key on LovyanGFX's emulated GPIO array.
 *
 * LovyanGFX's SDL panel can map an SDL keycode onto one of its emulated pins;
 * its event loop drives the pin LOW on key-down and HIGH on key-up. That is the
 * same active-LOW polarity as the real BOOT button on GPIO 9, so the harness
 * exercises the real tap-vs-hold timing rather than faking either outcome: a
 * quick SPACE press is a tap, holding SPACE for kBootResetHoldMs resets WiFi.
 *
 * The device latches taps in a GPIO ISR; there is no interrupt here, so the
 * edges are recovered by polling in bootButtonPollLongPress(). The latch
 * semantics are identical, which is what matters: a tap taken while the caller
 * is busy with blocking work is still consumed afterwards.
 */

#include "platform/wifi_setup.h"

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include "config.h"
#include "core/platform.h"

namespace {

/**
 * Mirrors config::kBootPin (GPIO 9). That constant lives in
 * platform/device/pins.h, which is device-only (gpio_num_t / ESP-IDF) and so
 * deliberately not includable from the native destination.
 */
constexpr uint8_t kNativeBootGpio = 9;

bool s_key_mapped = false;
bool s_boot_is_down = false;
unsigned long s_boot_down_ms = 0;
bool s_boot_tap_pending = false;
bool s_long_press_handled = false;

}  // namespace

void bootButtonInit() {
  if (s_key_mapped) {
    return;
  }
  // Idle state must be HIGH, matching pinMode(INPUT_PULLUP) on the device.
  // Panel_sdl::setup() raises every emulated pin, but this also covers being
  // called before the panel is up.
  lgfx::gpio_hi(kNativeBootGpio);
  lgfx::Panel_sdl::addKeyCodeMapping(SDLK_SPACE, kNativeBootGpio);
  s_key_mapped = true;
}

bool wifiBootButtonPressed() {
  // Active LOW: Panel_sdl drives the pin low for as long as the key is held.
  return lgfx::gpio_in(kNativeBootGpio) == false;
}

bool bootButtonConsumeTap() {
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  return tap;
}

void bootButtonPollLongPress() {
  // SDL pumps its events on the main thread while this runs on the user
  // thread, so the emulated GPIO byte is written by one thread and read by
  // another. Sample it once into a local: the read is racy but benign (a
  // single byte, no torn value to observe), and the worst case is that an edge
  // is noticed one 10 ms loop iteration late. Locking here would only buy
  // precision the SDL event timing does not have anyway.
  const bool down = wifiBootButtonPressed();
  const unsigned long now = core::platform::nowMs();

  if (down) {
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = now;
    }
    if (!s_long_press_handled &&
        now - s_boot_down_ms >= config::kBootResetHoldMs) {
      // Latched so the reset fires once per press, not once per poll.
      s_long_press_handled = true;
      core::platform::logf("BOOT held — resetting WiFi\n");
      wifiResetCredentialsAndReboot();
    }
    return;
  }

  if (s_boot_is_down) {
    // Release edge. The device's ISR classifies the press here; so do we.
    // Anything shorter than kBootTapMinMs is bounce, anything past the hold
    // threshold was already handled above as a long press.
    const unsigned long held = now - s_boot_down_ms;
    if (held >= config::kBootTapMinMs && held < config::kBootResetHoldMs) {
      s_boot_tap_pending = true;
    }
    s_boot_is_down = false;
  }
  s_long_press_handled = false;
}
