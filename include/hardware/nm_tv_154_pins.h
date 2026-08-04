#pragma once

#include <cstdint>

namespace hardware::nm_tv_154 {

constexpr uint8_t kLcdPowerPin = 21;
constexpr bool kLcdPowerEnabledLevel = false;
constexpr uint8_t kLcdBacklightPin = 19;
constexpr bool kLcdBacklightActiveLow = true;
constexpr uint8_t kTouchGpio = 32;  // ESP32 touch channel T9.

}  // namespace hardware::nm_tv_154
