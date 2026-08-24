/** Native display bring-up: LovyanGFX's SDL panel, 240x240 at 3x magnification. */

#include "ui/display.h"

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include <cstdio>
#include <cstdlib>

#include "config.h"
#include "core/platform.h"
#include "ui/display_font.h"

namespace {
/** Window magnification. The logical panel stays 240x240 so layout is identical. */
constexpr int kWindowScale = 3;
}  // namespace

LGFX tft(config::kDisplayWidth, config::kDisplayHeight, kWindowScale,
         kWindowScale);

void displayInit() {
  // Title must be set before init(): with no window yet, Panel_sdl only records
  // the string and SDL_CreateWindow applies it on the main thread. Setting it
  // afterwards calls SDL_SetWindowTitle from this worker thread, which SDL3
  // (via Homebrew's sdl2-compat) rejects — AppKit only allows window geometry
  // changes on the main thread. setWindowTitle lives on Panel_sdl, and
  // lgfx::LGFX keeps its panel instance private, so reach it through getPanel().
  static_cast<lgfx::Panel_sdl*>(tft.getPanel())->setWindowTitle("Plane Radar");
  tft.init();
  tft.setRotation(0);
  // No setBrightness: there is no backlight to dim on an SDL window.
  tft.setTextWrap(false);

  if (!displayFontInit()) {
    // Hard failure natively. Falling back to bitmap GFX fonts silently changes
    // every text metric the layout is derived from, so the harness would look
    // fine while misreporting exactly what it exists to measure.
    core::platform::logf(
        "FATAL: VLW smooth font failed to load; native rendering would not "
        "match the device.\n");
    std::fflush(stdout);
    std::exit(1);
  }
}
