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

constexpr unsigned long kTapMaxMs = 400UL;
constexpr unsigned long kInterTapMaxMs = 600UL;
constexpr unsigned long kTripleTapCooldownMs = 700UL;
constexpr int kTapMoveThresholdPx = 40;

uint8_t s_touch_addr = board::kTouchI2cAddrPrimary;
bool s_ready = false;

bool s_pinch_active = false;
float s_last_span_px = 0.0f;
unsigned long s_last_zoom_ms = 0;

bool s_touch_down = false;
unsigned long s_down_ms = 0;
int s_down_x = 0;
int s_down_y = 0;
int s_last_x = 0;
int s_last_y = 0;
uint8_t s_tap_count = 0;
unsigned long s_last_tap_ms = 0;
unsigned long s_last_triple_ms = 0;

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

void clearTouchStatus() {
  const uint8_t clear = 0;
  i2cWriteReg16(kGt911RegStatus, &clear, 1);
}

/**
 * One GT911 buffer read. had_event is true when the controller latched new data
 * (0x80). When false, callers should not treat count==0 as finger lift.
 */
bool readTouchFrame(bool& had_event, uint8_t& count, int& x0, int& y0, int& x1,
                    int& y1) {
  uint8_t status = 0;
  if (!i2cReadReg16(kGt911RegStatus, &status, 1)) {
    return false;
  }

  had_event = (status & 0x80) != 0;
  count = status & 0x0F;
  x0 = 0;
  y0 = 0;
  x1 = 0;
  y1 = 0;

  if (!had_event) {
    return true;
  }

  if (count >= 2) {
    uint8_t raw[16] = {};
    if (!i2cReadReg16(kGt911RegPoint1, raw, sizeof(raw))) {
      clearTouchStatus();
      return false;
    }
    x0 = static_cast<int>(raw[1]) | (static_cast<int>(raw[2]) << 8);
    y0 = static_cast<int>(raw[3]) | (static_cast<int>(raw[4]) << 8);
    x1 = static_cast<int>(raw[9]) | (static_cast<int>(raw[10]) << 8);
    y1 = static_cast<int>(raw[11]) | (static_cast<int>(raw[12]) << 8);
  } else if (count == 1) {
    uint8_t raw[8] = {};
    if (!i2cReadReg16(kGt911RegPoint1, raw, sizeof(raw))) {
      clearTouchStatus();
      return false;
    }
    x0 = static_cast<int>(raw[1]) | (static_cast<int>(raw[2]) << 8);
    y0 = static_cast<int>(raw[3]) | (static_cast<int>(raw[4]) << 8);
  }

  clearTouchStatus();
  return true;
}

float spanPx(int x0, int y0, int x1, int y1) {
  const float dx = static_cast<float>(x1 - x0);
  const float dy = static_cast<float>(y1 - y0);
  return std::sqrt(dx * dx + dy * dy);
}

void registerTap(unsigned long now) {
  if (s_tap_count > 0 && (now - s_last_tap_ms) > kInterTapMaxMs) {
    s_tap_count = 0;
  }
  s_tap_count++;
  s_last_tap_ms = now;
}

bool consumeTripleTap(unsigned long now) {
  if (s_tap_count < 3) {
    return false;
  }
  s_tap_count = 0;
  if (now - s_last_triple_ms < kTripleTapCooldownMs) {
    return false;
  }
  s_last_triple_ms = now;
  return true;
}

int pollPinchFromFrame(bool had_event, uint8_t count, int x0, int y0, int x1,
                       int y1) {
  if (!had_event) {
    return 0;
  }

  if (count < 2) {
    s_pinch_active = false;
    s_last_span_px = 0.0f;
    return 0;
  }

  s_touch_down = false;
  s_tap_count = 0;

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
  if (std::abs(delta) < static_cast<float>(kPinchDeltaThresholdPx)) {
    return 0;
  }

  s_last_zoom_ms = now;
  return (delta > 0.0f) ? 1 : -1;
}

bool pollTripleTapFromFrame(bool had_event, uint8_t count, int x0, int y0) {
  if (!had_event) {
    return false;
  }

  if (count >= 2) {
    return false;
  }

  const unsigned long now = millis();

  if (count == 1) {
    if (!s_touch_down) {
      s_touch_down = true;
      s_down_ms = now;
      s_down_x = x0;
      s_down_y = y0;
    }
    s_last_x = x0;
    s_last_y = y0;
    return false;
  }

  if (!s_touch_down) {
    return false;
  }

  s_touch_down = false;
  const unsigned long held = now - s_down_ms;
  if (held > kTapMaxMs) {
    return false;
  }

  const int dx = s_last_x - s_down_x;
  const int dy = s_last_y - s_down_y;
  if ((dx * dx + dy * dy) > (kTapMoveThresholdPx * kTapMoveThresholdPx)) {
    return false;
  }

  registerTap(now);
  return consumeTripleTap(now);
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
  Serial.printf("touch: GT911 @ 0x%02X (pinch zoom, triple-tap day mode)\n",
                s_touch_addr);
}

TouchGestureEvent touchGesturePoll() {
  TouchGestureEvent ev;
  if (!s_ready) {
    return ev;
  }

  bool had_event = false;
  uint8_t count = 0;
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  if (!readTouchFrame(had_event, count, x0, y0, x1, y1)) {
    s_pinch_active = false;
    s_last_span_px = 0.0f;
    return ev;
  }

  ev.pinch_zoom = pollPinchFromFrame(had_event, count, x0, y0, x1, y1);
  if (ev.pinch_zoom != 0) {
    return ev;
  }
  ev.color_mode_toggle = pollTripleTapFromFrame(had_event, count, x0, y0);
  return ev;
}

#else

void touchGestureInit() {}

TouchGestureEvent touchGesturePoll() { return {}; }

#endif
