#pragma once

/**
 * Device-only hardware configuration: GPIO assignments and panel bus settings.
 *
 * Split out of config.h so that config.h stays portable — it pulls in
 * <driver/gpio.h>, which does not exist on the native destination. Only the
 * device build (and the LGFX device config) may include this header.
 */

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;

// --- Display: GC9A01 1.28" round 240x240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert for correct black/green output
constexpr bool kDisplayInvert = true;

}  // namespace config
