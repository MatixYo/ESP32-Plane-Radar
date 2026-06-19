#include "hardware/display.h"

#include <Arduino.h>

#include "hardware/board.h"
#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  // Apply the runtime-selected board (NVS override, else compile default) before
  // init so a portal board change takes effect after reboot.
  const hardware::board::DisplayPins& p = hardware::board::activePins();
  tft.applyBoard(p);

  // Boards that gate the backlight on a GPIO (e.g. ESP32-2424S012) need it
  // driven high (active high). Driven directly rather than via LEDC so it does
  // not depend on PWM channel availability. Boards with a hard-wired backlight
  // use pin -1 and skip this.
  if (p.pin_backlight >= 0) {
    pinMode(p.pin_backlight, OUTPUT);
    digitalWrite(p.pin_backlight, HIGH);
  }

  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
