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
#include "core/tap_gesture.h"
#include "core/terrain.h"
#include "platform/png_decode.h"
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
bool g_terrain_download_active = false;

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

void onSiteTap() {
  if (core::settings::siteCount() < 2) {
    return;
  }
  core::settings::siteNext();
  core::adsb::clear();
  const char* ident = core::settings::siteActiveIdent();
  if (ident != nullptr) {
    pf::logf("Site: %s (%.6f, %.6f)\n", ident, core::settings::lat(),
             core::settings::lon());
  }
  if (g_radar_visible && wifiIsConnected()) {
    ui::radarDisplayDraw();
  }
  g_last_adsb_fetch_ms = pf::nowMs() - config::kAdsbFetchIntervalMs +
                         config::kAdsbMinRefetchMs;
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    core::gesture::tapPress(pf::nowMs());
  }
  switch (core::gesture::tapPoll(pf::nowMs())) {
    case core::gesture::Tap::kSingle:
      onRangeTap();
      break;
    case core::gesture::Tap::kDouble:
      onSiteTap();
      break;
    default:
      break;
  }
}

void pollWifiAndTaps() {
  wifiLoop();
  if (bootButtonConsumeTap()) {
    core::gesture::tapPress(pf::nowMs());
  }
}

/**
 * Download the terrain grid for the current view when it is missing, then
 * repaint so the new background shows. gridReady() makes the common case a
 * cheap no-op, and ensureGrid() rate-limits retries after a failed download,
 * so this is safe to call every loop iteration.
 */
void maybeFetchTerrain() {
  if (!g_radar_visible || !wifiIsConnected() || !ui::radar::showTerrain()) {
    return;
  }
  const double lat = core::settings::lat();
  const double lon = core::settings::lon();
  const uint8_t range_idx = ui::radar::rangeIndex();
  if (core::terrain::gridReady(lat, lon, range_idx)) {
    return;
  }
  const bool ready = core::terrain::ensureGrid(lat, lon, range_idx,
                                               ui::radar::terrainHalfSpanKm());
  const bool active = core::terrain::downloadActive();
  // Each decoded tile leaves the frame sprite full of the decoder's workings, so
  // repaint on the edge where a download stops — on success to show the terrain,
  // otherwise to clean up after it. Only on the edge: the retry gate leaves this
  // false a minute at a time, and repainting every loop would be a full redraw
  // every 10 ms.
  if (ready || (g_terrain_download_active && !active)) {
    ui::radarDisplayDraw();
  }
  g_terrain_download_active = active;
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
  core::adsb::setPollFn(pollWifiAndTaps);
  core::terrain::setPollFn(pollWifiAndTaps);
  core::terrain::setPngDecoder(platform_png::decode);
  platform_png::setScratch(ui::radarDisplayFrameScratch);

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
    maybeFetchTerrain();
  }

  pf::sleepMs(10);
}
