#pragma once

#include <cstdint>

#include <driver/gpio.h>

#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
#include "board/esp32_p4_waveshare_4c.h"
#elif defined(PLANE_RADAR_BOARD_ESP32_TFT_ROUND)
#include "board/esp32_tft_round.h"
#else
#include "board/supermini.h"
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace config {

constexpr char kFirmwareVersion[] = "1.2.0";

namespace defaults {
constexpr double kRadarLat = 52.3676;
constexpr double kRadarLon = 4.9041;
}  // namespace defaults

#if defined(PLANE_RADAR_SECRETS_ACTIVE) && PLANE_RADAR_SECRETS_ACTIVE
constexpr bool kWifiSkipPortal = plane_radar_secrets::kWifiSkipPortal;
constexpr const char* kWifiSsid = plane_radar_secrets::kWifiSsid;
constexpr const char* kWifiPass = plane_radar_secrets::kWifiPass;
constexpr double kDefaultRadarLat =
    plane_radar_secrets::kOverrideDefaultLocation
        ? plane_radar_secrets::kDefaultRadarLat
        : defaults::kRadarLat;
constexpr double kDefaultRadarLon =
    plane_radar_secrets::kOverrideDefaultLocation
        ? plane_radar_secrets::kDefaultRadarLon
        : defaults::kRadarLon;
#else
constexpr bool kWifiSkipPortal = false;
constexpr const char* kWifiSsid = "";
constexpr const char* kWifiPass = "";
constexpr double kDefaultRadarLat = defaults::kRadarLat;
constexpr double kDefaultRadarLon = defaults::kRadarLon;
#endif

constexpr bool kEnableWifiLongPressReset = board::kEnableWifiLongPressReset;

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

// --- User button (board profile in include/board/*.h) ---
constexpr gpio_num_t kBootPin = board::kBootPin;
constexpr bool kBootPinInternalPullup = board::kBootPinInternalPullup;
constexpr gpio_num_t kWifiResetPin = board::kWifiResetPin;
constexpr bool kWifiResetPinInternalPullup = board::kWifiResetPinInternalPullup;
constexpr bool kBootUsePolledTap = board::kBootUsePolledTap;
constexpr gpio_num_t kRangePin2 = board::kRangePin2;
constexpr bool kButtonActiveLow = board::kButtonActiveLow;
constexpr unsigned long kBootResetHoldMs = board::kBootResetHoldMs;
constexpr uint8_t kBootLowStreakRequired = board::kBootLowStreakRequired;
constexpr uint8_t kWifiBootConnectAttempts = 1;
constexpr uint8_t kButtonStableSamplesRequired = board::kButtonStableSamplesRequired;
constexpr unsigned long kButtonTapCooldownMs = board::kButtonTapCooldownMs;
constexpr uint8_t kButtonReadSamples = board::kButtonReadSamples;
constexpr uint8_t kButtonReadThreshold = board::kButtonReadThreshold;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = board::kBootTapMinMs;

// --- Display ---
constexpr gpio_num_t kDisplayPinRst = board::kDisplayPinRst;
constexpr gpio_num_t kDisplayPinBl = board::kDisplayPinBl;

#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
constexpr gpio_num_t kDisplayPinCs = board::kDisplayPinCs;
constexpr gpio_num_t kDisplayPinDc = board::kDisplayPinDc;
constexpr gpio_num_t kDisplayPinMosi = board::kDisplayPinMosi;
constexpr gpio_num_t kDisplayPinSclk = board::kDisplayPinSclk;
constexpr uint32_t kDisplaySpiWriteHz = board::kDisplaySpiWriteHz;
constexpr bool kDisplayInvert = board::kDisplayInvert;
constexpr bool kDisplayRgbOrder = board::kDisplayRgbOrder;
#else
#include <driver/spi_master.h>
constexpr gpio_num_t kDisplayPinCs = board::kDisplayPinCs;
constexpr gpio_num_t kDisplayPinDc = board::kDisplayPinDc;
constexpr gpio_num_t kDisplayPinMosi = board::kDisplayPinMosi;
constexpr gpio_num_t kDisplayPinSclk = board::kDisplayPinSclk;
constexpr spi_host_device_t kDisplaySpiHost = board::kDisplaySpiHost;
constexpr uint32_t kDisplaySpiWriteHz = board::kDisplaySpiWriteHz;
constexpr bool kDisplayInvert = board::kDisplayInvert;
constexpr bool kDisplayRgbOrder = board::kDisplayRgbOrder;
#endif

constexpr int kDisplayBaseSize = 240;
#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
constexpr int kDisplayWidth = board::kDisplayWidth;
constexpr int kDisplayHeight = board::kDisplayHeight;
#else
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;
#endif

#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
constexpr bool kDisplayBacklightInvert = board::kDisplayBacklightInvert;
constexpr gpio_num_t kTouchPinSda = board::kTouchPinSda;
constexpr gpio_num_t kTouchPinScl = board::kTouchPinScl;
#endif

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
