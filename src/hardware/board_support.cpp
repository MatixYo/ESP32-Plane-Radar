#include "hardware/board_support.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {

#if defined(BOARD_ONX2432G028) || defined(BOARD_ONX3248G035)

constexpr uint8_t kPcf8574Addr = 0x38;
constexpr int kPcfExioLcdRst = 6;
constexpr int kPcfExioSdCs = 7;
constexpr int kBoardI2cSda = 8;
constexpr int kBoardI2cScl = 7;

uint8_t s_pcf_shadow = 0xFF;
bool s_pcf_ready = false;

bool pcfWrite(uint8_t value) {
  Wire.beginTransmission(kPcf8574Addr);
  Wire.write(value);
  const uint8_t error = Wire.endTransmission();
  if (error != 0) {
    Serial.printf("board: PCF8574 write failed (%u)\n", error);
    return false;
  }
  s_pcf_shadow = value;
  return true;
}

bool pcfSetOutput(int bit, bool high) {
  const uint8_t mask = static_cast<uint8_t>(1u << bit);
  const uint8_t next = high ? (s_pcf_shadow | mask) : (s_pcf_shadow & ~mask);
  return pcfWrite(next);
}

void initOnxIoExpander() {
  if (s_pcf_ready) {
    return;
  }

  Wire.begin(kBoardI2cSda, kBoardI2cScl);
  Wire.setClock(400000);

  s_pcf_ready = pcfWrite(0xFF);
  if (!s_pcf_ready) {
    Serial.println("board: continuing without PCF8574 init");
    return;
  }

  pcfSetOutput(kPcfExioSdCs, true);
  pcfSetOutput(kPcfExioLcdRst, false);
  delay(200);
  pcfSetOutput(kPcfExioLcdRst, true);
  delay(200);
}

#endif

}  // namespace

void boardInitBeforeDisplay() {
  if (config::kDisplayPinBl != GPIO_NUM_NC) {
    pinMode(config::kDisplayPinBl, OUTPUT);
    digitalWrite(config::kDisplayPinBl, LOW);
  }

#if defined(BOARD_ONX2432G028) || defined(BOARD_ONX3248G035)
  initOnxIoExpander();
#endif
}
