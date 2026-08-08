/**
 * Native entry point.
 *
 * Runs the real, shared setup()/loop() from src/main.cpp under LovyanGFX's SDL
 * driver. Panel_sdl::main() owns the true main thread and pumps SDL events
 * there, calling user_func on a worker — which is exactly the shape Arduino
 * gives us on the device, so main.cpp needs no native special-casing.
 *
 * -DPLANE_RADAR_FRAME_HASH additionally dumps the derived font metrics and a
 * hash of the composited frame at startup. Those are the fidelity contract
 * against the device; see docs/fidelity-baseline.txt.
 */

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#include "core/platform.h"

#if defined(PLANE_RADAR_FRAME_HASH)
#include "ui/display.h"
#include "ui/display_font.h"
#include "ui/radar_theme.h"
#endif

// Defined in src/main.cpp, shared verbatim with the device build.
void setup();
void loop();

namespace {

#if defined(PLANE_RADAR_FRAME_HASH)

namespace pf = core::platform;

/** Mirrors radar_display.cpp's findVlwSizeForHeight() binary search exactly. */
float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    tft.setTextSize(mid);
    if (tft.fontHeight() < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void dumpFontMetrics() {
  pf::logf("--- font metrics (compare against the device) ---\n");
  pf::logf("  smooth(VLW): %d\n", displayFontIsSmooth() ? 1 : 0);
  pf::logf("  blob bytes:  %zu\n", pf::fontBlobLen());

  displayFontEnsureLoaded(tft);

  const float cardinal = findVlwSizeForHeight(ui::radar::kCardinalLabelHeightPx);
  tft.setTextSize(cardinal);
  const int cardinal_h = tft.fontHeight();
  const float scale =
      findVlwSizeForHeight(cardinal_h - ui::radar::kScaleBelowCardinalPx);
  const float tag = findVlwSizeForHeight(ui::radar::kAircraftTagLabelHeightPx);

  pf::logf("  cardinal size=%.6f height=%d\n", static_cast<double>(cardinal),
           cardinal_h);
  pf::logf("  scale    size=%.6f\n", static_cast<double>(scale));
  pf::logf("  tag      size=%.6f\n", static_cast<double>(tag));

  static const char* const kStrings[] = {"N",       "25km", "15mi",
                                         "KLM1234", "B738", "EHAM"};
  tft.setTextSize(scale);
  pf::logf("  textWidth @scale:");
  for (const char* s : kStrings) {
    pf::logf(" %s=%d", s, tft.textWidth(s));
  }
  pf::logf("\n");

  tft.setTextSize(tag);
  pf::logf("  textWidth @tag:  ");
  for (const char* s : kStrings) {
    pf::logf(" %s=%d", s, tft.textWidth(s));
  }
  pf::logf("\n------------------------------------------------\n");
}

#endif  // PLANE_RADAR_FRAME_HASH

int user_func(bool* running) {
  setup();
#if defined(PLANE_RADAR_FRAME_HASH)
  dumpFontMetrics();
#endif
  do {
    loop();
  } while (*running);
  return 0;
}

}  // namespace

int main(int, char**) { return lgfx::Panel_sdl::main(user_func); }
