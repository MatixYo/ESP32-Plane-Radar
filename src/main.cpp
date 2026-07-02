/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/route.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
bool g_dialog_open = false;

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

// Tap a flight to open a details dialog; tap again (anywhere) to close it.
void handleTouch() {
  int32_t tx = 0;
  int32_t ty = 0;
  const bool touched = tft.getTouch(&tx, &ty);
  static bool was_touched = false;

  if (touched && !was_touched) {  // act on touch-down edge only
    if (g_dialog_open) {
      g_dialog_open = false;
      showRadarIfConnected();  // repaint radar under the dialog
    } else {
      const int idx = ui::radarDisplayHitTest(static_cast<int>(tx),
                                               static_cast<int>(ty));
      if (idx >= 0) {
        g_dialog_open = true;
        services::route::RouteInfo route;
        route.valid = false;
        if (ui::radar::dialogFieldEnabled(ui::radar::DialogField::kRoute)) {
          const services::adsb::Aircraft& ac =
              services::adsb::aircraftList()[static_cast<size_t>(idx)];
          if (ac.callsign[0] != '\0') {
            services::route::lookup(ac.callsign, &route);
          }
        }
        ui::radarDisplayDrawDialog(idx, &route);
      }
    }
  }
  was_touched = touched;
}

// Called by the ADS-B client during its (blocking) network I/O, so the WiFi
// portal stays alive AND the sweep keeps animating instead of freezing.
void radarPoll() {
  wifiLoop();
  if (g_radar_visible && !g_dialog_open && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayAnimate();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
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
  services::adsb::setPollFn(radarPoll);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  handleTouch();
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
    if (!g_dialog_open) {
      if (!g_radar_visible) {
        showRadarIfConnected();
      } else if (millis() - g_last_adsb_fetch_ms >=
                 ui::radar::adsbFetchIntervalMs()) {
        g_last_adsb_fetch_ms = millis();
        fetchAndDrawAircraft();
      }
      ui::radarDisplayAnimate();  // rotating sweep over the cached radar
    }
  }

  delay(10);
}
