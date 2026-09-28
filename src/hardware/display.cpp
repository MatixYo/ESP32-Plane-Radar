#include "hardware/display.h"

#include <Arduino.h>

#include "config.h"
#include "hardware/display_font.h"

LGFX tft;

void displayInit()
{
  // ESP32-2424S012 LCD backlight is active HIGH on GPIO 3.
  pinMode(static_cast<int>(config::kDisplayPinBl), OUTPUT);
  digitalWrite(static_cast<int>(config::kDisplayPinBl), HIGH);

  tft.init();
  tft.setRotation(0);
  tft.setTextWrap(false);

  displayFontInit();
}