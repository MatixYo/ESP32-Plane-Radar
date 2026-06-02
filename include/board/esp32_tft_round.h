#pragma once

/** HackerBox #0107 / ESP32-TFT 88267 — ESP32-D0WD + integrated GC9A01 round panel.
 *  Pin map matches official kit TFT_eSPI Setup200_GC9A01 (Instructables HackerBox 0107). */
#include <driver/gpio.h>
#include <driver/spi_master.h>

namespace board {

constexpr gpio_num_t kBootPin = GPIO_NUM_34;
constexpr bool kBootPinInternalPullup = false;
constexpr gpio_num_t kWifiResetPin = GPIO_NUM_35;
constexpr bool kWifiResetPinInternalPullup = false;
/** GPIO 34–39 have no internal pull-up; poll in loop with heavy debounce. */
constexpr bool kBootUsePolledTap = true;
/** Only GPIO 34 cycles range; GPIO 35 floats on this PCB. */
constexpr gpio_num_t kRangePin2 = GPIO_NUM_NC;
constexpr bool kButtonActiveLow = true;
constexpr unsigned long kBootResetHoldMs = 5000UL;
constexpr uint8_t kBootLowStreakRequired = 8;
constexpr uint8_t kButtonStableSamplesRequired = 10;
constexpr unsigned long kBootTapMinMs = 120UL;
constexpr unsigned long kButtonTapCooldownMs = 600UL;
constexpr uint8_t kButtonReadSamples = 5;
constexpr uint8_t kButtonReadThreshold = 4;

/** GPIO 35 floats — disable long-press Wi-Fi wipe unless external pull-ups added. */
constexpr bool kEnableWifiLongPressReset = false;

/** microSD (if populated) — not used by Plane Radar; do not drive GPIO 5 (display CS). */
constexpr gpio_num_t kSdCardPinCs = GPIO_NUM_NC;

constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_15;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_14;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_5;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_27;
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_33;
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_22;

constexpr spi_host_device_t kDisplaySpiHost = SPI2_HOST;

constexpr uint32_t kDisplaySpiWriteHz = 27000000;
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = false;

}  // namespace board
