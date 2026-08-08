/** Device display bring-up: GC9A01 over SPI. */

#include "ui/display.h"

#include "ui/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
