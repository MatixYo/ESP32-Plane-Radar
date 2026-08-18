/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 *
 * Shared by both destinations. Everything platform-specific goes through
 * core::platform or the wifi_setup.h seam, so this file compiles unchanged for
 * the device and for the native harness.
 */

#include "config.h"
#include "core/platform.h"
#include "ui/display.h"
#include "core/adsb.h"
#include "core/settings.h"
#include "platform/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace pf = core::platform;

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;

void showRadarIfConnected() {
  if (!wifiIsConnected()) {
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
  pf::logf("Range: %s (outer ~%.0f km)\n", range_label,
           static_cast<double>(ui::radar::rangeCurrent().outer_km));

  if (g_radar_visible && wifiIsConnected()) {
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
  if (!core::adsb::fetchUpdate(core::settings::lat(), core::settings::lon(),
                               fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  pf::logInit();
  pf::logf("\nPlane Radar\n");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  core::settings::init();
  ui::radar::rangeInit();
  core::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (!wifiIsConnected()) {
    if (g_radar_visible) {
      pf::logf("WiFi lost — will reconnect\n");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = pf::nowMs();
    }

    const unsigned long down_ms = pf::nowMs() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        pf::nowMs() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = pf::nowMs();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (pf::nowMs() - g_last_adsb_fetch_ms >=
               config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = pf::nowMs();
      fetchAndDrawAircraft();
    }
  }

  pf::sleepMs(10);
}
