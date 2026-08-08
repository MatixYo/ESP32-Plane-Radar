#pragma once

#include <cstddef>

namespace core::adsb {

/**
 * One decoded ADS-B target.
 *
 * Tag strings are fixed-size and pre-formatted at parse time so the render path
 * does no allocation and no formatting.
 */
struct Aircraft {
  float lat;
  float lon;
  float nose_deg;   ///< where the airframe points
  float track_deg;  ///< where it is actually going
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];
};

/** Hard cap on tracked targets; the render path allocates arrays of this size. */
constexpr size_t kMaxAircraft = 64;

}  // namespace core::adsb
