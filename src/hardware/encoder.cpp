#include "hardware/encoder.h"

#include <Arduino.h>
#include "config.h"

namespace hardware::encoder {

namespace {

portMUX_TYPE s_encoder_mux = portMUX_INITIALIZER_UNLOCKED;

// Table-based quadrature decoder
const int8_t kEncoderTable[16] = {
     0,  // 00 -> 00
    -1,  // 00 -> 01
     1,  // 00 -> 10
     0,  // 00 -> 11
     1,  // 01 -> 00
     0,  // 01 -> 01
     0,  // 01 -> 10
    -1,  // 01 -> 11
    -1,  // 10 -> 00
     0,  // 10 -> 01
     0,  // 10 -> 10
     1,  // 10 -> 11
     0,  // 11 -> 00
     1,  // 11 -> 01
    -1,  // 11 -> 10
     0   // 11 -> 11
};

volatile uint8_t s_prev_state = 0x03;
volatile int8_t s_accum = 0;
volatile int16_t s_rot_steps = 0;
volatile unsigned long s_last_rot_ms = 0;

volatile bool s_btn_is_down = false;
volatile unsigned long s_btn_down_ms = 0;
volatile uint8_t s_tap_count = 0;
volatile unsigned long s_last_tap_ms = 0;
volatile bool s_long_press_reported = false;

void IRAM_ATTR isrEncoder() {
  const uint8_t clk = digitalRead(config::kEncoderPinClk);
  const uint8_t dt = digitalRead(config::kEncoderPinDt);
  const uint8_t curr = (clk << 1) | dt;
  const unsigned long now = millis();

  portENTER_CRITICAL_ISR(&s_encoder_mux);
  s_last_rot_ms = now;  // Mark rotation activity to suppress cross-talk misclicks!

  if (curr != (s_prev_state & 0x03)) {
    const uint8_t index = ((s_prev_state & 0x03) << 2) | curr;
    const int8_t step = kEncoderTable[index];
    s_accum += step;
    s_prev_state = curr;

    if (curr == 0x03) {
      if (s_accum >= 2) {
        s_rot_steps++;
      } else if (s_accum <= -2) {
        s_rot_steps--;
      }
      s_accum = 0;
    } else if (s_accum >= 4) {
      s_rot_steps++;
      s_accum = 0;
    } else if (s_accum <= -4) {
      s_rot_steps--;
      s_accum = 0;
    }
  }
  portEXIT_CRITICAL_ISR(&s_encoder_mux);
}

void IRAM_ATTR isrButton() {
  const bool down = (digitalRead(config::kEncoderPinSw) == LOW);
  const unsigned long now = millis();

  portENTER_CRITICAL_ISR(&s_encoder_mux);
  // Completely reject button triggers while rotating (cross-talk suppression)
  if (now - s_last_rot_ms < 200) {
    s_btn_is_down = false;
    portEXIT_CRITICAL_ISR(&s_encoder_mux);
    return;
  }

  if (down) {
    s_btn_is_down = true;
    s_btn_down_ms = now;
    s_long_press_reported = false;
  } else if (s_btn_is_down) {
    const unsigned long held = now - s_btn_down_ms;
    // Human tap must be sustained for at least 35ms (filtering bounce) and less than 600ms
    if (held >= 35 && held < 600) {
      s_tap_count++;
      s_last_tap_ms = now;
    }
    s_btn_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_encoder_mux);
}

}  // namespace

void init() {
  // Virtual GND pin: GPIO 8 is held LOW (0V)
  pinMode(config::kEncoderPinGnd, OUTPUT);
  digitalWrite(config::kEncoderPinGnd, LOW);

  // Virtual VCC pin: GPIO 2 is held HIGH (3.3V)
  pinMode(config::kEncoderPinVcc, OUTPUT);
  digitalWrite(config::kEncoderPinVcc, HIGH);

  pinMode(config::kEncoderPinClk, INPUT_PULLUP);
  pinMode(config::kEncoderPinDt, INPUT_PULLUP);
  pinMode(config::kEncoderPinSw, INPUT_PULLUP);

  uint8_t clk = digitalRead(config::kEncoderPinClk);
  uint8_t dt = digitalRead(config::kEncoderPinDt);
  s_prev_state = (clk << 1) | dt;

  attachInterrupt(digitalPinToInterrupt(config::kEncoderPinClk), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(config::kEncoderPinDt), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(config::kEncoderPinSw), isrButton, CHANGE);
}

Rotation pollRotation() {
  Rotation result = Rotation::NONE;
  portENTER_CRITICAL(&s_encoder_mux);
  if (s_rot_steps > 0) {
    result = Rotation::CW;
    s_rot_steps--;
  } else if (s_rot_steps < 0) {
    result = Rotation::CCW;
    s_rot_steps++;
  }
  portEXIT_CRITICAL(&s_encoder_mux);
  return result;
}

ButtonAction pollButtonAction() {
  ButtonAction act = ButtonAction::NONE;
  portENTER_CRITICAL(&s_encoder_mux);
  const unsigned long now = millis();

  // If turning knob, discard any noise pulses on button
  if (now - s_last_rot_ms < 200) {
    s_tap_count = 0;
    s_btn_is_down = false;
  } else if (s_btn_is_down && !s_long_press_reported && (now - s_btn_down_ms >= 700)) {
    s_long_press_reported = true;
    s_tap_count = 0;
    act = ButtonAction::LONG_PRESS;
  } else if (s_tap_count >= 2) {
    s_tap_count = 0;
    act = ButtonAction::DOUBLE_CLICK;
  } else if (s_tap_count == 1 && (now - s_last_tap_ms >= 280)) {
    s_tap_count = 0;
    act = ButtonAction::CLICK;
  }
  portEXIT_CRITICAL(&s_encoder_mux);
  return act;
}

bool pollButtonClick() {
  return pollButtonAction() == ButtonAction::CLICK;
}

}  // namespace hardware::encoder
