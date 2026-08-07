#include "hardware/display.h"

#include <Arduino.h>

#include "hardware/display_font.h"
#ifdef BOARD_NM_TV_154
#include "hardware/nm_tv_154_pins.h"
#endif

LGFX tft;

void displayInit() {
#ifdef BOARD_NM_TV_154
  pinMode(hardware::nm_tv_154::kLcdPowerPin, OUTPUT);
  digitalWrite(hardware::nm_tv_154::kLcdPowerPin,
               hardware::nm_tv_154::kLcdPowerEnabledLevel ? HIGH : LOW);
  delay(50);
#endif
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
#ifdef BOARD_NM_TV_154
  Serial.println(
      "display: ST7789 240x240 mem=240x320 offset=(0,0) rotation=0 "
      "order=BGR invert=ON spi=MODE3 pwr=GPIO21/LOW bl=GPIO19/LOW");
  tft.fillRect(0, 0, 80, 240, 0xF800);
  tft.fillRect(80, 0, 80, 240, 0x07E0);
  tft.fillRect(160, 0, 80, 240, 0x001F);
  delay(750);
  tft.fillScreen(0x0000);
#endif
  displayFontInit();
}
