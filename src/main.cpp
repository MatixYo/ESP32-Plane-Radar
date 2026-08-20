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

/**
 * Rewind g_last_adsb_fetch_ms so the next loop iteration is free to fetch
 * again (subject only to kAdsbMinRefetchMs) instead of waiting out the rest
 * of kAdsbFetchIntervalMs. A subtraction from "now" rather than an absolute
 * deadline, so it stays correct across the millis() rollover. Re-stamping on
 * every tap also keeps rapid tapping inside adsb.fi's 1 req/s limit.
 */
void scheduleAdsbFetchSoon() {
  g_last_adsb_fetch_ms = pf::nowMs() - config::kAdsbFetchIntervalMs +
                         config::kAdsbMinRefetchMs;
}

/** Fired by core::settings whenever lat()/lon() actually move. */
void onCenterChanged() {
  core::adsb::clear();
  core::terrain::clear();
}

/** State change plus NVS only — the drain loop in handleBootButton() owns the
 *  (single, post-drain) repaint. Returns whether anything actually changed. */
bool applyRangeNext() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  pf::logf("Range: %s (outer ~%.0f km)\n", range_label,
           static_cast<double>(ui::radar::rangeCurrent().outer_km));
  // Zooming out multiplies fetchRadiusKm(); without this the cached store
  // keeps covering the old, narrower radius for up to kAdsbFetchIntervalMs.
  scheduleAdsbFetchSoon();
  return true;
}

/** State change plus NVS only, mirroring applyRangeNext(). */
bool applySiteNext() {
  if (core::settings::siteCount() < 2) {
    return false;
  }
  core::settings::siteNext();
  // No core::adsb::clear() here: the onCenterChanged() hook registered in
  // setup() now covers every path that moves the centre, tap or portal alike.
  const char* ident = core::settings::siteActiveIdent();
  if (ident != nullptr) {
    pf::logf("Site: %s (%.6f, %.6f)\n", ident, core::settings::lat(),
             core::settings::lon());
  }
  scheduleAdsbFetchSoon();
  return true;
}

/** Applies the gesture's state change; returns whether a repaint is owed. */
bool dispatchTap(core::gesture::Tap tap) {
  switch (tap) {
    case core::gesture::Tap::kSingle:
      return applyRangeNext();
    case core::gesture::Tap::kDouble:
      return applySiteNext();
    default:
      return false;
  }
}

void handleBootButton() {
  bootButtonPollLongPress();

  bool needs_repaint = false;
  unsigned long tap_ms = 0;
  while (bootButtonConsumeTap(&tap_ms)) {
    // Load-bearing, not incidental: resolve whatever gesture is already
    // pending at the NEW tap's own release timestamp before recording that
    // tap. Polling at "now" instead would let a single left pending from a
    // stale blocking window (an in-flight ADS-B fetch or terrain tile)
    // get silently swallowed the moment an unrelated later tap shows up,
    // instead of correctly resolving as its own single.
    if (dispatchTap(core::gesture::tapPoll(tap_ms))) {
      needs_repaint = true;
    }
    core::gesture::tapPress(tap_ms);
  }
  if (dispatchTap(core::gesture::tapPoll(pf::nowMs()))) {
    needs_repaint = true;
  }
  // Draining can yield more than one gesture per loop iteration; repaint once
  // here rather than once per gesture inside dispatchTap — a full
  // radarDisplayDraw() costs roughly 128 ms.
  if (needs_repaint && g_radar_visible && wifiIsConnected()) {
    ui::radarDisplayDraw();
  }
}

/**
 * Poll hook handed to core::adsb / core::terrain for use during blocking HTTP
 * I/O. Forwards to wifiLoop() and nothing else — no tap consumption, no
 * gesture dispatch, no draw. That absence is structural, not just documented:
 * no gesture code is reachable from here, so a frame can never be composed
 * while the PNG decoder holds the frame sprite as scratch. See
 * docs/plan/findings-01-bugs.md (the compose-during-decode invariant) and
 * findings-02-memory.md MEM-10 (which borrows the same scratch), both of
 * which depend on this poll path staying draw-free.
 */
void pollWifi() { wifiLoop(); }

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
  // After init(), not before: init() seeds s_lat/s_lon from storage, and the
  // hook must not fire on that initial load, only on later moves.
  core::settings::setCenterChangedFn(onCenterChanged);
  ui::radar::rangeInit();
  core::adsb::setPollFn(pollWifi);
  core::terrain::setPollFn(pollWifi);
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
