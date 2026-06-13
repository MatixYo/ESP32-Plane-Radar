#pragma once

/** Waveshare ESP32-P4-WIFI6-Touch-LCD-4C — 720×720 round MIPI DSI (JD9365). */
#include <driver/gpio.h>

namespace board {

constexpr gpio_num_t kBootPin = GPIO_NUM_35;
constexpr bool kBootPinInternalPullup = true;
constexpr gpio_num_t kWifiResetPin = GPIO_NUM_NC;
constexpr bool kWifiResetPinInternalPullup = false;
constexpr bool kBootUsePolledTap = false;
constexpr gpio_num_t kRangePin2 = GPIO_NUM_NC;
constexpr bool kButtonActiveLow = true;
constexpr unsigned long kBootResetHoldMs = 5000UL;
constexpr uint8_t kBootLowStreakRequired = 1;
constexpr uint8_t kButtonStableSamplesRequired = 1;
constexpr unsigned long kBootTapMinMs = 120UL;
constexpr unsigned long kButtonTapCooldownMs = 600UL;
constexpr uint8_t kButtonReadSamples = 1;
constexpr uint8_t kButtonReadThreshold = 1;

/** Hardcoded Wi-Fi — no portal long-press wipe. */
constexpr bool kEnableWifiLongPressReset = false;

constexpr gpio_num_t kSdCardPinCs = GPIO_NUM_NC;

/** MIPI DSI panel reset / backlight (xiaozhi Waveshare P4 config). */
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_27;
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_26;
constexpr bool kDisplayBacklightInvert = true;

/** Not used on DSI path. */
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_NC;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_NC;
constexpr uint32_t kDisplaySpiWriteHz = 0;
constexpr bool kDisplayInvert = false;
constexpr bool kDisplayRgbOrder = true;

constexpr int kDisplayWidth = 720;
constexpr int kDisplayHeight = 720;

/** MIPI DSI PHY / timing (Waveshare 4C). */
constexpr uint16_t kMipiLaneMbps = 1500;
constexpr uint8_t kMipiLaneNum = 2;
constexpr uint8_t kMipiLdoChan = 3;
constexpr uint32_t kMipiLdoVoltageMv = 2500;
constexpr uint16_t kDpiClockMhz = 46;
constexpr uint16_t kDpiHsyncPulse = 20;
constexpr uint16_t kDpiHsyncBackPorch = 20;
constexpr uint16_t kDpiHsyncFrontPorch = 40;
constexpr uint16_t kDpiVsyncPulse = 4;
constexpr uint16_t kDpiVsyncBackPorch = 12;
constexpr uint16_t kDpiVsyncFrontPorch = 24;

/** GT911 touch on shared codec I2C bus. */
constexpr gpio_num_t kTouchPinSda = GPIO_NUM_7;
constexpr gpio_num_t kTouchPinScl = GPIO_NUM_8;
constexpr uint8_t kTouchI2cAddrPrimary = 0x5D;
constexpr uint8_t kTouchI2cAddrBackup = 0x14;

}  // namespace board
