/**
 * Plane Radar — WiFi setup, then radar UI on the round display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/touch_gesture.h"
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

void onRangeChanged() {
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayRefreshRange();
  }
}

void onRangeTap() {
  ui::radar::rangeNext();
  onRangeChanged();
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

void handleTouchGestures() {
  const int pinch = touchGesturePollPinchZoom();
  if (pinch > 0) {
    ui::radar::rangeNext();
    onRangeChanged();
  } else if (pinch < 0) {
    ui::radar::rangePrev();
    onRangeChanged();
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    handleTouchGestures();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
  handleTouchGestures();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");
  Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));

  bootButtonInit();
  displayInit();
  displayBacklightOn();
  touchGestureInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
    if (g_radar_visible) {
      fetchAndDrawAircraft();
    }
  }
  bootButtonEnableWifiReset();
}

void loop() {
  handleBootButton();
  handleTouchGestures();

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
