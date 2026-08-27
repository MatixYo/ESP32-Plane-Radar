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

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

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

// --- Flight route lookup (origin / destination on the aircraft tag) ---
/** false = never query routes; tag shows only callsign / type / altitude. */
constexpr bool kRouteLookupEnabled = true;
/** api.adsbdb.com callsign endpoint (one HTTPS GET per unresolved callsign). */
constexpr char kRouteApiBase[] = "https://api.adsbdb.com/v0/callsign/";
/** New callsigns resolved per ADS-B poll (keeps the fetch cycle short). */
constexpr uint8_t kRouteLookupsPerCycle = 3;
/** Resolved (and "no route") entries cached in RAM; LRU-evicted past this. */
constexpr size_t kRouteCacheSize = 48;
/** Re-query a callsign that firmly returned no route only after this (ms). */
constexpr unsigned long kRouteNegativeTtlMs = 600000UL;  // 10 min
/** Shorter re-query delay after a soft failure (timeout, TLS OOM, bad body). */
constexpr unsigned long kRouteRetryTtlMs = 120000UL;  // 2 min

// --- Aircraft track history (breadcrumb trail behind each target) ---
/** Fixes kept per aircraft; × kAdsbFetchIntervalMs ≈ how far back the trail
 *  goes (60 × 3 s = 3 min, enough for a full turn). RAM: kTrackHistoryMax ×
 *  this × 8 B. */
constexpr size_t kTrackHistoryDepth = 60;
/** Distinct aircraft tracked at once (least-recently-seen evicted past this).
 *  On-screen targets refresh every poll so they are never the eviction victim. */
constexpr size_t kTrackHistoryMax = 24;
/** Forget a track with no fresh fix for this long (survives a brief dropout so
 *  a long trail is not wiped by one missed fetch). */
constexpr unsigned long kTrackHistoryTtlMs = 60000UL;
/** Ignore a fix within this squared-degree distance of the previous one
 *  (~1e-4° ≈ 11 m) so a parked/duplicate report does not fill the ring. */
constexpr float kTrackHistoryMinStepDeg2 = 1.0e-8f;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
