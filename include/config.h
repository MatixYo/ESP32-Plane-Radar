#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
constexpr char kPortalHostname[] = "plane-radar";
constexpr char kPortalHostUrl[] = "plane-radar.local";

constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;
constexpr unsigned long kWifiConnectingFrameMs = 50;
constexpr unsigned long kWifiDownGraceMs = 4000;
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

// --- Satellite tracker ---
constexpr char kTleGroupUrl[] =
    "https://celestrak.org/NORAD/elements/gp.php?GROUP=visual&FORMAT=tle";

/** TLE-Katalog (Bahndaten) neu laden — Bahndaten ändern sich langsam. */
constexpr unsigned long kTleCatalogRefreshIntervalMs = 6UL * 60UL * 60UL * 1000UL;  // 6 h

/** Position neu berechnen — kein Download, daher kann das häufig laufen. */
constexpr unsigned long kSatellitePositionIntervalMs = 8000;  // 8 s

/** 0° = Horizont. Höher setzen, um nur deutlich sichtbare Pässe zu zeigen. */
constexpr float kSatelliteMinElevationDeg = 0.0f;

/** Standort-Höhe über Meeresspiegel in Metern (kein GPS, daher fix). */
constexpr double kDefaultObserverAltM = 50.0;

constexpr char kNtpServer[] = "pool.ntp.org";

}  // namespace config
