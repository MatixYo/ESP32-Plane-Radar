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
constexpr unsigned long kAdsbFetchIntervalMs = 5000;
/** Redraw cadence; aircraft are dead-reckoned along their track/speed so motion
 *  is smooth. ~4 Hz = 250 ms (panel scanout caps at ~24 Hz; the full-frame
 *  recompose+present is the practical limit ~10-15 Hz). */
constexpr unsigned long kRadarRedrawIntervalMs = 250;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

// --- MQTT / Home Assistant ---
/** Defaults applied when the matching portal field is left EMPTY. The firmware
 *  ships no broker address on purpose: host/user/pass are typed by the user in
 *  the config portal, and an empty host keeps MQTT inert. */
constexpr uint16_t kMqttDefaultPort = 1883;
constexpr char kMqttDefaultTopicPrefix[] = "planeradar";
constexpr char kMqttDefaultDiscoveryPrefix[] = "homeassistant";
constexpr char kMqttDefaultDeviceName[] = "Plane Radar";
/** Telemetry cadence once discovery is published. */
constexpr unsigned long kMqttStateIntervalMs = 10000UL;
/** Gap between discovery publishes: the socket needs time to drain. */
constexpr unsigned long kMqttDiscoverySpacingMs = 250UL;
/** Minimum gap between connect attempts after a failure. */
constexpr unsigned long kMqttReconnectIntervalMs = 5000UL;
/** Skip telemetry below this free heap: publishing a 640 B payload with almost
 *  nothing left is what turns a rough patch into a dead radar. */
constexpr size_t kMqttMinFreeHeap = 8000;
/** Re-publish the retained discovery after this long without one, as insurance
 *  against a broker that restarted and lost the retained topics. */
constexpr unsigned long kMqttDiscoveryRefreshMs = 600000UL;
/** Advertised in dev.sw. */
constexpr char kMqttSwVersion[] = "1.1.0-mqtt";
/** Must match the -DMQTT_MAX_PACKET_SIZE build flag (asserted in mqtt_client.cpp):
 *  the ceiling covers topic + payload + header, not the payload alone. */
constexpr size_t kMqttPacketSize = 768;

}  // namespace config
