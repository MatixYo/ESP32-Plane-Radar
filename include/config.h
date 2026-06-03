#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://plane-radar.local */
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- Board pin profiles (selected via -DBOARD_* in platformio.ini) ---
// Both boards drive a GC9A01(A) 1.28" round 240×240 panel over SPI.
#if defined(BOARD_S3_WAVESHARE_TOUCH128)

// Waveshare ESP32-S3-Touch-LCD-1.28 (ESP32-S3R2)
constexpr gpio_num_t kBootPin = GPIO_NUM_0;  // BOOT0 button, active LOW
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_14;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_9;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_8;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_11;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_10;  // display SCL
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_2;     // backlight (active HIGH)
#define DISPLAY_HAS_BACKLIGHT 1

// CST816S capacitive touch (I2C). A tap cycles the range preset (no BOOT button).
constexpr gpio_num_t kTouchPinSda = GPIO_NUM_6;
constexpr gpio_num_t kTouchPinScl = GPIO_NUM_7;
constexpr gpio_num_t kTouchPinInt = GPIO_NUM_5;
constexpr gpio_num_t kTouchPinRst = GPIO_NUM_13;
constexpr uint8_t kTouchI2cAddr = 0x15;
/** Debounce: ignore a repeat tap within this window of the last one. */
constexpr unsigned long kTouchTapDebounceMs = 250UL;
#define DISPLAY_HAS_TOUCH 1

#else  // BOARD_C3_SUPERMINI (default)

// ESP32-C3 Super Mini
constexpr gpio_num_t kBootPin = GPIO_NUM_9;  // BOOT button, active LOW
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL
constexpr gpio_num_t kDisplayPinBl = GPIO_NUM_NC;   // backlight tied to power (no GPIO)

#endif

constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 3000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
