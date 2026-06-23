#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

#if defined(BOARD_ONX3248G035)
constexpr bool kBoardOnx3248g035 = true;
#else
constexpr bool kBoardOnx3248g035 = false;
#endif

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

// --- User input (active LOW when present) ---
constexpr bool kHasBootButton = true;
constexpr bool kBootTapChangesRange = true;
constexpr gpio_num_t kBootPin =
    kBoardOnx3248g035 ? GPIO_NUM_0 : GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display ---
constexpr gpio_num_t kDisplayPinRst =
    kBoardOnx3248g035 ? GPIO_NUM_NC : GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs =
    kBoardOnx3248g035 ? GPIO_NUM_2 : GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc =
    kBoardOnx3248g035 ? GPIO_NUM_3 : GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi =
    kBoardOnx3248g035 ? GPIO_NUM_1 : GPIO_NUM_3;
constexpr gpio_num_t kDisplayPinSclk =
    kBoardOnx3248g035 ? GPIO_NUM_5 : GPIO_NUM_4;
constexpr gpio_num_t kDisplayPinBl =
    kBoardOnx3248g035 ? GPIO_NUM_6 : GPIO_NUM_NC;

constexpr int kDisplayWidth = kBoardOnx3248g035 ? 320 : 240;
constexpr int kDisplayHeight = kBoardOnx3248g035 ? 480 : 240;

constexpr int kRadarViewportSize = kDisplayWidth < 240 ? kDisplayWidth : 240;
constexpr int kRadarViewportX = (kDisplayWidth - kRadarViewportSize) / 2;
constexpr int kRadarViewportY = (kDisplayHeight - kRadarViewportSize) / 2;
constexpr bool kFrameSpriteUsePsram = kBoardOnx3248g035;

constexpr uint32_t kDisplaySpiWriteHz =
    kBoardOnx3248g035 ? 40000000 : 40000000;
constexpr bool kDisplayInvert = kBoardOnx3248g035 ? false : true;
constexpr bool kDisplayRgbOrder = true;
constexpr uint8_t kDisplayRotation = 0;

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
