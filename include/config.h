#pragma once

/**
 * Portable configuration shared by every destination.
 *
 * Must stay free of Arduino / ESP-IDF headers: the native destination compiles
 * this file too. Device-only pin and bus settings live in
 * include/platform/device/pins.h.
 */

#include <cstdint>

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

// --- BOOT button timing (pin lives in platform/device/pins.h) ---
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;
/** Second tap within this window counts as a double tap. */
constexpr unsigned long kDoubleTapWindowMs = 500UL;

// --- Display geometry (pins and bus settings in platform/device/pins.h) ---
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

/**
 * GC9A01 panel colour order.
 *
 * TODO(step 1): pending the on-hardware colour check. This is currently read by
 * both the LGFX device config and ui/radar_display.cpp's initPalette(), which
 * software-swaps R/B for the aircraft colour only. Once the panel's real colour
 * order is confirmed the swap is deleted and this constant moves to pins.h.
 */
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 47.0753;
constexpr double kDefaultRadarLon = 15.4062;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 10000;
/** Minimum gap after a manual site switch before the next adsb.fi request. */
constexpr unsigned long kAdsbMinRefetchMs = 1000;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
