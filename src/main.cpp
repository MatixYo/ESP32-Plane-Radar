/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }

  // After successful aircraft fetch, try to resolve destinations for
  // any planes we don't yet have route info for. Limit to one route
  // lookup per refresh cycle to avoid hammering the API.
  const size_t n = services::adsb::aircraftCount();
  services::adsb::Aircraft* planes = const_cast<services::adsb::Aircraft*>(
      services::adsb::aircraftList());
  
  Serial.printf("main: %u aircraft, checking routes...\n", static_cast<unsigned>(n));
  
  for (size_t i = 0; i < n; ++i) {
    Serial.printf("main: plane[%u] callsign='%s' dest='%s'\n", 
                  static_cast<unsigned>(i), 
                  planes[i].callsign[0] ? planes[i].callsign : "(none)",
                  planes[i].dest[0] ? planes[i].dest : "(unknown)");
    if (planes[i].callsign[0] == '\0') continue;
    if (planes[i].dest[0] != '\0') continue;  // Already known

    char fetched_dest[5] = {};
    Serial.printf("main: fetching route for %s\n", planes[i].callsign);
    if (services::adsb::fetchRoute(planes[i].callsign, fetched_dest, sizeof(fetched_dest))) {
      Serial.printf("main: got dest %s for %s\n", fetched_dest, planes[i].callsign);
      strncpy(planes[i].dest, fetched_dest, sizeof(planes[i].dest) - 1);
      planes[i].dest[sizeof(planes[i].dest) - 1] = '\0';
      planes[i].dest_fetched_ms = millis();
    } else {
      Serial.printf("main: route fetch failed for %s\n", planes[i].callsign);
    }
    break;  // One lookup per cycle
  }

  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
