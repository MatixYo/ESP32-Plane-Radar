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
volatile bool g_adsb_fetch_requested = false;
volatile bool g_adsb_fetch_running = false;
volatile bool g_adsb_refresh_ready = false;

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

void fetchAircraftTask(void*) {
  for (;;) {
    if (!g_adsb_fetch_requested) {
      delay(10);
      continue;
    }
    g_adsb_fetch_requested = false;
    g_adsb_fetch_running = true;

    const float fetch_km = ui::radar::fetchRadiusKm();
    if (services::adsb::fetchUpdate(services::location::lat(),
                                    services::location::lon(), fetch_km)) {
      g_adsb_refresh_ready = true;
    }
    g_adsb_fetch_running = false;
  }
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
  services::adsb::setPollFn(nullptr);
  if (xTaskCreate(fetchAircraftTask, "adsb-fetch", 8192, nullptr, 1,
                  nullptr) != pdPASS) {
    Serial.println("adsb: failed to create fetch task");
  }

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
    } else if (!g_adsb_fetch_running && !g_adsb_fetch_requested &&
               millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      g_adsb_fetch_requested = true;
    }
    if (g_adsb_refresh_ready) {
      g_adsb_refresh_ready = false;
      ui::radarDisplayRefreshAircraft();
    }
    if (g_radar_visible) {
      ui::radarDisplayAnimate();
    }
  }

  // Keep animation cadence fine-grained; a 10 ms loop delay quantizes a
  // requested 25 ms sweep interval into visibly uneven 30/40 ms steps.
  delay(1);
}
