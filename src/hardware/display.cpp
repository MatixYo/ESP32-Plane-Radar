#include "hardware/display.h"

#include "config.h"
#include "hardware/display_font.h"

LGFX tft;

static void deselectSharedSpiDevices() {
#if defined(PLANE_RADAR_BOARD_ESP32_TFT_ROUND)
  if (board::kSdCardPinCs != GPIO_NUM_NC &&
      board::kSdCardPinCs != board::kDisplayPinCs) {
    pinMode(static_cast<uint8_t>(board::kSdCardPinCs), OUTPUT);
    digitalWrite(static_cast<uint8_t>(board::kSdCardPinCs), HIGH);
  }
#endif
}

void displayInit() {
  deselectSharedSpiDevices();
  if (config::kDisplayPinBl != GPIO_NUM_NC) {
    pinMode(static_cast<uint8_t>(config::kDisplayPinBl), OUTPUT);
    digitalWrite(static_cast<uint8_t>(config::kDisplayPinBl), LOW);
  }
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
  tft.fillScreen(config::kColorBlack);
}

void displayBacklightOn() {
  if (config::kDisplayPinBl != GPIO_NUM_NC) {
    digitalWrite(static_cast<uint8_t>(config::kDisplayPinBl), HIGH);
  }
}
