#pragma once

/**
 * Radar projection: lat/lon to screen pixels, and the ring geometry around it.
 *
 * Extracted from ui/radar_display.cpp's anonymous namespace, where it read the
 * radar centre and the active range preset from globals. Everything here now
 * takes an explicit Viewport, which makes it pure, unit-testable on the host,
 * and usable for a centre other than "wherever the device happens to be".
 */

namespace core::geo {

/** Flat-earth approximation, consistent with the original implementation. */
constexpr float kKmPerDeg = 111.0f;

/** Everything the projection needs to know about the current view. */
struct Viewport {
  double center_lat = 0.0;
  double center_lon = 0.0;
  int center_x = 0;          ///< screen centre, px
  int center_y = 0;
  int outer_radius_px = 0;   ///< outermost grid ring, px
  float outer_km = 0.0f;     ///< ground distance at outer_radius_px
};

struct Point {
  int x = 0;
  int y = 0;
};

/** Ground offset from the radar centre. North is +dy, east is +dx. */
struct Offset {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
};

Offset offsetKmFromCenter(const Viewport& vp, float lat, float lon);

/** Project to screen pixels: 1 degree ~ 111 km, north = screen up. */
Point latLonToScreen(const Viewport& vp, float lat, float lon);

/**
 * Largest ground distance whose symbol still fits inside the outer ring.
 *
 * `inset_px` keeps the symbol's centroid far enough inside that its nose and
 * wings do not overhang the ring.
 */
float innerRingMaxKm(const Viewport& vp, int inset_px);

bool isInsideOuterRingKm(const Viewport& vp, float dist_km, int inset_px);

/** Squared pixel distance from the viewport centre. */
int distSqFromCenter(const Viewport& vp, int x, int y);

/**
 * Direction cue for traffic beyond the outer ring: a point on the screen rim at
 * the target's true bearing. Correct in direction, not in distance.
 *
 * Returns false when the target is inside the ring, or too close to the centre
 * for a bearing to be meaningful.
 */
bool rimPointForDistantTarget(const Viewport& vp, float lat, float lon,
                              int inset_px, int rim_radius_px, Point* out);

/**
 * Walk (x1,y1) back toward (x0,y0) until it lies within `max_r` of the centre.
 *
 * Used to clip an aircraft's speed vector at the outer ring. Collapses the
 * point onto (x0,y0) when no part of the segment fits.
 */
void clipPointToOuterRing(const Viewport& vp, int max_r, int x0, int y0,
                          int* x1, int* y1);

}  // namespace core::geo
