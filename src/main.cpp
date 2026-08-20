/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/airport_cache.h"
#include "services/radar_location.h"
#include "services/route_cache.h"
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

bool planeNeedsRouteLookup(const services::adsb::Aircraft& plane) {
  if (plane.callsign[0] == '\0') {
    return false;
  }
  if (plane.origin[0] != '\0' || plane.origin_name[0] != '\0' ||
      plane.dest[0] != '\0' || plane.dest_name[0] != '\0') {
    return false;
  }
  if (!services::route_cache::contains(plane.callsign)) {
    return true;
  }
  services::route_cache::RouteInfo info;
  services::route_cache::clearRouteInfo(&info);
  if (!services::route_cache::lookup(plane.callsign, &info)) {
    return true;
  }
  // Cached ICAO without names — retry airport decode.
  const bool origin_pending =
      info.origin_icao[0] != '\0' && info.origin_iata[0] == '\0' &&
      info.origin_name[0] == '\0';
  const bool dest_pending = info.dest_icao[0] != '\0' &&
                            info.dest_iata[0] == '\0' && info.dest_name[0] == '\0';
  return origin_pending || dest_pending;
}

void applyRouteInfo(services::adsb::Aircraft* plane,
                    const services::route_cache::RouteInfo& info) {
  strncpy(plane->origin, info.origin_iata, sizeof(plane->origin) - 1);
  plane->origin[sizeof(plane->origin) - 1] = '\0';
  strncpy(plane->origin_icao, info.origin_icao, sizeof(plane->origin_icao) - 1);
  plane->origin_icao[sizeof(plane->origin_icao) - 1] = '\0';
  strncpy(plane->origin_name, info.origin_name, sizeof(plane->origin_name) - 1);
  plane->origin_name[sizeof(plane->origin_name) - 1] = '\0';
  strncpy(plane->dest, info.dest_iata, sizeof(plane->dest) - 1);
  plane->dest[sizeof(plane->dest) - 1] = '\0';
  strncpy(plane->dest_icao, info.dest_icao, sizeof(plane->dest_icao) - 1);
  plane->dest_icao[sizeof(plane->dest_icao) - 1] = '\0';
  strncpy(plane->dest_name, info.dest_name, sizeof(plane->dest_name) - 1);
  plane->dest_name[sizeof(plane->dest_name) - 1] = '\0';
  if (services::route_cache::routeHasDisplay(info)) {
    plane->route_fetched_ms = millis();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }

  const size_t n = services::adsb::aircraftCount();
  services::adsb::Aircraft* planes = const_cast<services::adsb::Aircraft*>(
      services::adsb::aircraftList());

  for (size_t i = 0; i < n; ++i) {
    if (!planeNeedsRouteLookup(planes[i])) {
      continue;
    }

    services::route_cache::RouteInfo info;
    services::route_cache::clearRouteInfo(&info);
    Serial.printf("main: fetching route for %s\n", planes[i].callsign);
    if (services::adsb::fetchRoute(planes[i].callsign, &info)) {
      applyRouteInfo(&planes[i], info);
      Serial.printf("main: %s  %s -> %s\n", planes[i].callsign,
                    planes[i].origin[0] ? planes[i].origin
                                        : (planes[i].origin_name[0]
                                               ? planes[i].origin_name
                                               : "?"),
                    planes[i].dest[0] ? planes[i].dest
                                      : (planes[i].dest_name[0]
                                             ? planes[i].dest_name
                                             : "?"));
    } else {
      Serial.printf("main: no route for %s\n", planes[i].callsign);
    }
    break;
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
  services::airport_cache::init();
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
