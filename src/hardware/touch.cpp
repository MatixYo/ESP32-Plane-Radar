#include "hardware/touch.h"

#include "config.h"

#ifdef DISPLAY_HAS_TOUCH

#include <Arduino.h>
#include <Wire.h>

namespace {

// INT pulses LOW on a touch event. We poll the finger-count register over I2C
// each call; the ISR flag only catches a quick tap that started AND ended while
// a blocking section (e.g. an HTTP fetch) kept us from polling.
volatile bool s_int_fired = false;
bool s_finger_down = false;
unsigned long s_last_tap_ms = 0;

void IRAM_ATTR onTouchIsr() { s_int_fired = true; }

/** Read one CST816S register; returns 0 on any I2C error. */
uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(config::kTouchI2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) {
    return 0;
  }
  if (Wire.requestFrom(static_cast<int>(config::kTouchI2cAddr), 1) != 1) {
    return 0;
  }
  return Wire.read();
}

}  // namespace

void touchInit() {
  // Hardware reset: low pulse, then let the controller boot.
  pinMode(config::kTouchPinRst, OUTPUT);
  digitalWrite(config::kTouchPinRst, LOW);
  delay(10);
  digitalWrite(config::kTouchPinRst, HIGH);
  delay(60);

  Wire.begin(static_cast<int>(config::kTouchPinSda),
             static_cast<int>(config::kTouchPinScl), 400000);

  pinMode(config::kTouchPinInt, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kTouchPinInt)),
                  onTouchIsr, FALLING);
}

bool touchConsumeTap() {
  const bool down = readReg(0x02) > 0;  // CST816S reg 0x02 = finger count (0/1)
  const bool press_edge = down && !s_finger_down;

  // A tap that began and ended between polls: INT fired but finger is up again.
  const bool missed = s_int_fired && !down && !s_finger_down;
  s_int_fired = false;
  s_finger_down = down;

  // Only the down-edge fires, so holding a finger never repeats; the timer
  // just rejects contact bounce that would double-count one tap.
  if (!press_edge && !missed) {
    return false;
  }
  const unsigned long now = millis();
  if (now - s_last_tap_ms < config::kTouchTapDebounceMs) {
    return false;
  }
  s_last_tap_ms = now;
  return true;
}

#else  // no touch panel on this board

void touchInit() {}
bool touchConsumeTap() { return false; }

#endif
