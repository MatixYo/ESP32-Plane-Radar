#include "hardware/display.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/board_support.h"
#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  boardInitBeforeDisplay();
  tft.init();
  tft.setRotation(config::kDisplayRotation);
  if (config::kDisplayPinBl != GPIO_NUM_NC) {
    digitalWrite(config::kDisplayPinBl, HIGH);
  }
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}

void displayRadarBackground() { tft.fillScreen(config::kColorBlack); }
