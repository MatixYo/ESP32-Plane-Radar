#include "ui/radar_runways.h"

#include "ui/radar_geo.h"
#include "ui/radar_theme.h"

namespace ui::radar {

namespace {

/** Runway segment from FAA published threshold coordinates (AirNav). */
struct RunwaySegment {
  double lat1;
  double lon1;
  double lat2;
  double lon2;
};

/** KSFF Felts Field, KGEG Spokane Intl, KSKA Fairchild AFB. */
constexpr RunwaySegment kLocalRunways[] = {
    // Felts — 04L/22R
    {47.6792003, -117.3313870, 47.6864162, -117.3165778},
    // Felts — 04R/22L
    {47.6810402, -117.3241463, 47.6852893, -117.3154213},
    // GEG — 03/21
    {47.6100808, -117.5500799, 47.6312169, -117.5182659},
    // GEG — 08/26
    {47.6169635, -117.5532678, 47.6167678, -117.5200290},
    // Fairchild — 05/23
    {47.6075250, -117.6816833, 47.6225833, -117.6299195},
};

void drawRunwaySegment(lgfx::LGFXBase& gfx, const RunwaySegment& rwy) {
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
  geo::latLonToScreen(static_cast<float>(rwy.lat1),
                      static_cast<float>(rwy.lon1), &x1, &y1);
  geo::latLonToScreen(static_cast<float>(rwy.lat2),
                      static_cast<float>(rwy.lon2), &x2, &y2);

  gfx.drawWideLine(x1, y1, x2, y2, radar::kRunwayLineHalfWidth,
                   radar::kColorRunway);
}

}  // namespace

void drawRunways(lgfx::LGFXBase& gfx) {
  for (const RunwaySegment& rwy : kLocalRunways) {
    drawRunwaySegment(gfx, rwy);
  }
}

}  // namespace ui::radar
