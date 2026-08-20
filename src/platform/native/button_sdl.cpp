/**
 * Native BOOT button: a keyboard key on LovyanGFX's emulated GPIO array.
 *
 * LovyanGFX's SDL panel can map an SDL keycode onto one of its emulated pins;
 * its event loop drives the pin LOW on key-down and HIGH on key-up. That is the
 * same active-LOW polarity as the real BOOT button on GPIO 9, so the harness
 * exercises the real tap-vs-hold timing rather than faking either outcome: a
 * quick SPACE press is a tap, holding SPACE for config::kBootResetHoldMs resets
 * WiFi. Both thresholds live with the classifier in core::button, which is why
 * this file needs no config.h of its own.
 *
 * There is no interrupt here, so both edges are recovered by polling in
 * bootButtonPollLongPress(). Classification itself is no longer duplicated:
 * this poll and the device's ISR feed the same core::button::Tracker, so
 * debounce, hold detection and the timestamped tap queue are shared code. Only
 * delivery still differs — a press and release that both fall between two polls
 * is never seen here, where the device's GPIO latch would report it late.
 */

#include "platform/wifi_setup.h"

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include "core/button_edges.h"
#include "core/platform.h"

namespace {

/**
 * Mirrors config::kBootPin (GPIO 9). That constant lives in
 * platform/device/pins.h, which is device-only (gpio_num_t / ESP-IDF) and so
 * deliberately not includable from the native destination.
 */
constexpr uint8_t kNativeBootGpio = 9;

bool s_key_mapped = false;
/** Single-threaded by construction: only bootButtonPollLongPress() writes it. */
core::button::Tracker s_tracker{};

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

bool bootButtonConsumeTap(unsigned long* tap_ms) {
  return core::button::popTap(s_tracker, tap_ms);
}

void bootButtonPollLongPress() {
  // SDL pumps its events on the main thread while this runs on the user
  // thread, so the emulated GPIO byte is written by one thread and read by
  // another. Sample it once into a local: the read is racy but benign (a
  // single byte, no torn value to observe), and the worst case is that an edge
  // is noticed one 10 ms loop iteration late. Locking here would only buy
  // precision the SDL event timing does not have anyway.
  const bool down = wifiBootButtonPressed();
  core::button::sample(s_tracker, down, core::platform::nowMs());
  if (core::button::popLongPress(s_tracker)) {
    core::platform::logf("BOOT held — resetting WiFi\n");
    wifiResetCredentialsAndReboot();
  }
}
