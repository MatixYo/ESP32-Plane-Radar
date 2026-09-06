#include "hardware/display.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/display_font.h"
#include "ui/radar_theme.h"

LGFX tft;

namespace {

/** Longest we will wait for a TE edge before giving up and pushing anyway. */
constexpr uint32_t kTearingEffectTimeoutMs = 25;

}  // namespace

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();

  if (config::kDisplayPinTe >= 0) {
    pinMode(config::kDisplayPinTe, INPUT);
  }

  // Panel size comes from the driver rather than config.h, so the layout
  // follows whichever panel is actually fitted.
  ui::radar::initMetrics(tft.width(), tft.height());
}

void displayWaitForFrameStart() {
  if (config::kDisplayPinTe < 0) {
    return;
  }

  // Ride out any pulse already in progress, then wait for the next rising
  // edge. Catching the edge rather than the level is what puts the push at the
  // start of the blanking interval instead of somewhere inside it.
  const uint32_t deadline = millis() + kTearingEffectTimeoutMs;
  while (digitalRead(config::kDisplayPinTe) == HIGH) {
    if (millis() >= deadline) {
      return;
    }
  }
  while (digitalRead(config::kDisplayPinTe) == LOW) {
    if (millis() >= deadline) {
      return;
    }
  }
}
