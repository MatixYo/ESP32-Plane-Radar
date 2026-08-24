#include "core/geo.h"

#include <cmath>

namespace core::geo {

Offset offsetKmFromCenter(const Viewport& vp, float lat, float lon) {
  Offset o;
  o.dx_km = static_cast<float>(lon - vp.center_lon) * kKmPerDeg;
  o.dy_km = static_cast<float>(lat - vp.center_lat) * kKmPerDeg;
  o.dist_km = sqrtf(o.dx_km * o.dx_km + o.dy_km * o.dy_km);
  return o;
}

Point latLonToScreen(const Viewport& vp, float lat, float lon) {
  const float px_per_km =
      static_cast<float>(vp.outer_radius_px) / vp.outer_km;
  const Offset o = offsetKmFromCenter(vp, lat, lon);

  Point p;
  p.x = vp.center_x + static_cast<int>(lroundf(o.dx_km * px_per_km));
  p.y = vp.center_y - static_cast<int>(lroundf(o.dy_km * px_per_km));
  return p;
}

float innerRingMaxKm(const Viewport& vp, int inset_px) {
  return vp.outer_km * (static_cast<float>(vp.outer_radius_px - inset_px) /
                        static_cast<float>(vp.outer_radius_px));
}

bool isInsideOuterRingKm(const Viewport& vp, float dist_km, int inset_px) {
  return dist_km <= innerRingMaxKm(vp, inset_px);
}

int distSqFromCenter(const Viewport& vp, int x, int y) {
  const int dx = x - vp.center_x;
  const int dy = y - vp.center_y;
  return dx * dx + dy * dy;
}

bool rimPointForDistantTarget(const Viewport& vp, float lat, float lon,
                              int inset_px, int rim_radius_px, Point* out) {
  const Offset o = offsetKmFromCenter(vp, lat, lon);
  if (o.dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(vp, o.dist_km, inset_px)) {
    return false;
  }

  const float angle_rad = atan2f(o.dx_km, o.dy_km);
  out->x = vp.center_x +
           static_cast<int>(lroundf(sinf(angle_rad) * rim_radius_px));
  out->y = vp.center_y -
           static_cast<int>(lroundf(cosf(angle_rad) * rim_radius_px));
  return true;
}

void clipPointToOuterRing(const Viewport& vp, int max_r, int x0, int y0,
                          int* x1, int* y1) {
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(vp, *x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(vp, px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

}  // namespace core::geo
