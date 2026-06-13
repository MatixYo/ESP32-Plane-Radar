#include "hardware/touch_gesture.h"

#include "config.h"

#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)

#include <Arduino.h>
#include <Wire.h>

#include <cmath>

namespace {

constexpr uint16_t kGt911RegStatus = 0x814E;
constexpr uint16_t kGt911RegPoint1 = 0x8150;
constexpr uint32_t kI2cHz = 400000UL;
constexpr int kPinchDeltaThresholdPx = 40;
constexpr unsigned long kPinchCooldownMs = 600UL;

uint8_t s_touch_addr = board::kTouchI2cAddrPrimary;
bool s_ready = false;
bool s_pinch_active = false;
float s_last_span_px = 0.0f;
unsigned long s_last_zoom_ms = 0;

bool i2cWriteReg16(uint16_t reg, const uint8_t* data, size_t len) {
  Wire.beginTransmission(s_touch_addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  for (size_t i = 0; i < len; ++i) {
    Wire.write(data[i]);
  }
  return Wire.endTransmission() == 0;
}

bool i2cReadReg16(uint16_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(s_touch_addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const int got = Wire.requestFrom(static_cast<int>(s_touch_addr),
                                   static_cast<int>(len));
  if (got != static_cast<int>(len)) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool probeTouchAddr(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool readTwoPoints(int& x0, int& y0, int& x1, int& y1) {
  uint8_t status = 0;
  if (!i2cReadReg16(kGt911RegStatus, &status, 1)) {
    return false;
  }
  const uint8_t count = status & 0x0F;
  if (count < 2) {
    const uint8_t clear = 0;
    i2cWriteReg16(kGt911RegStatus, &clear, 1);
    return false;
  }

  uint8_t raw[16] = {};
  if (!i2cReadReg16(kGt911RegPoint1, raw, sizeof(raw))) {
    return false;
  }

  x0 = static_cast<int>(raw[1]) | (static_cast<int>(raw[2]) << 8);
  y0 = static_cast<int>(raw[3]) | (static_cast<int>(raw[4]) << 8);
  x1 = static_cast<int>(raw[9]) | (static_cast<int>(raw[10]) << 8);
  y1 = static_cast<int>(raw[11]) | (static_cast<int>(raw[12]) << 8);

  const uint8_t clear = 0;
  i2cWriteReg16(kGt911RegStatus, &clear, 1);
  return true;
}

float spanPx(int x0, int y0, int x1, int y1) {
  const float dx = static_cast<float>(x1 - x0);
  const float dy = static_cast<float>(y1 - y0);
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

void touchGestureInit() {
  Wire.begin(static_cast<int>(config::kTouchPinSda),
             static_cast<int>(config::kTouchPinScl));
  Wire.setClock(kI2cHz);
  if (probeTouchAddr(board::kTouchI2cAddrPrimary)) {
    s_touch_addr = board::kTouchI2cAddrPrimary;
  } else if (probeTouchAddr(board::kTouchI2cAddrBackup)) {
    s_touch_addr = board::kTouchI2cAddrBackup;
  } else {
    Serial.println("touch: GT911 not found on I2C");
    return;
  }
  s_ready = true;
  Serial.printf("touch: GT911 @ 0x%02X (pinch-to-zoom)\n", s_touch_addr);
}

int touchGesturePollPinchZoom() {
  if (!s_ready) {
    return 0;
  }

  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  if (!readTwoPoints(x0, y0, x1, y1)) {
    s_pinch_active = false;
    s_last_span_px = 0.0f;
    return 0;
  }

  const float span = spanPx(x0, y0, x1, y1);
  if (!s_pinch_active) {
    s_pinch_active = true;
    s_last_span_px = span;
    return 0;
  }

  const float delta = span - s_last_span_px;
  s_last_span_px = span;

  const unsigned long now = millis();
  if (now - s_last_zoom_ms < kPinchCooldownMs) {
    return 0;
  }
  if (std::abs(delta) < kPinchDeltaThresholdPx) {
    return 0;
  }

  s_last_zoom_ms = now;
  return (delta > 0.0f) ? 1 : -1;
}

#else

void touchGestureInit() {}

int touchGesturePollPinchZoom() { return 0; }

#endif
