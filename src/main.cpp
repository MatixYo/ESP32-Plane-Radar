#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "services/wifi_setup.h"
#include "services/radar_location.h"
#include "services/satellite_tracker.h"
#include "ui/radar_display.h"
#include "ui/status_screens.h"
#include "hardware/display.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_position_update_ms = 0;
unsigned long g_last_tle_refresh_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void recomputeAndDraw() {
  services::satellites::recomputePositions(services::location::lat(),
                                           services::location::lon(),
                                           config::kDefaultObserverAltM);
  ui::radarDisplayRefreshSatellites();
  g_last_position_update_ms = millis();
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
      recomputeAndDraw();  // sofort, kein Download nötig
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Satellite Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  services::satellites::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    services::satellites::syncTime();
    services::satellites::refreshTleCatalog();
    g_last_tle_refresh_ms = millis();
    showRadarIfConnected();
    if (g_radar_visible) {
      recomputeAndDraw();
    }
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
        services::satellites::syncTime();
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else {
      if (millis() - g_last_tle_refresh_ms >= config::kTleCatalogRefreshIntervalMs) {
        g_last_tle_refresh_ms = millis();
        services::satellites::refreshTleCatalog();
      }
      if (millis() - g_last_position_update_ms >= config::kSatellitePositionIntervalMs) {
        recomputeAndDraw();
      }
    }
  }

  delay(10);
}
