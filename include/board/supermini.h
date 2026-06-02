#pragma once

#include <driver/gpio.h>
#include <driver/spi_master.h>

namespace board {

constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr bool kBootPinInternalPullup = true;
constexpr gpio_num_t kWifiResetPin = GPIO_NUM_NC;
constexpr bool kWifiResetPinInternalPullup = false;
constexpr bool kBootUsePolledTap = false;
constexpr gpio_num_t kRangePin2 = GPIO_NUM_NC;
constexpr bool kButtonActiveLow = true;
constexpr unsigned long kBootResetHoldMs = 3000UL;
constexpr uint8_t kBootLowStreakRequired = 1;
constexpr uint8_t kButtonStableSamplesRequired = 1;
constexpr unsigned long kBootTapMinMs = 40UL;
constexpr unsigned long kButtonTapCooldownMs = 0UL;
constexpr uint8_t kButtonReadSamples = 1;
constexpr uint8_t kButtonReadThreshold = 1;

constexpr bool kEnableWifiLongPressReset = true;

constexpr gpio_num_t kSdCardPinCs = GPIO_NUM_NC;

constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_NC;

constexpr spi_host_device_t kDisplaySpiHost = SPI2_HOST;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

}  // namespace board
