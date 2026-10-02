#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "services/adsb_client.h"

namespace services::adsb {

/** Nearest aircraft measured from the radar centre. */
struct NearestAircraft {
  bool valid;
  char callsign[9];
  char type[5];
  char alt[12];
  float distance_km;
  float gs_knots;
  float track_deg;
};

namespace detail {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.017453292519943295f;

inline float distanceSqKm(double lat0, double lon0, float cos_lat, float lat1,
                          float lon1) {
  const float dlat = (lat1 - static_cast<float>(lat0)) * kKmPerDeg;
  const float dlon = (lon1 - static_cast<float>(lon0)) * kKmPerDeg * cos_lat;
  return dlat * dlat + dlon * dlon;
}

}  // namespace detail

/** Flat-earth ground distance in km; longitude scaled by cos(lat0), matching
 *  the renderer's projection. */
inline float flatDistanceKm(double lat0, double lon0, float lat1, float lon1) {
  const float cos_lat = cosf(static_cast<float>(lat0) * detail::kDegToRad);
  return sqrtf(detail::distanceSqKm(lat0, lon0, cos_lat, lat1, lon1));
}

/** Closest entry of list[0..count) to (lat0, lon0); valid=false when count==0. */
inline NearestAircraft findNearest(const Aircraft* list, size_t count, double lat0,
                                   double lon0) {
  NearestAircraft best{};
  best.valid = false;
  if (list == nullptr || count == 0) {
    return best;
  }

  // cos(lat0) is loop-invariant: on this FPU-less chip a cosf() per item is the
  // difference between microseconds and milliseconds.
  const float cos_lat = cosf(static_cast<float>(lat0) * detail::kDegToRad);
  float best_sq = 0.0f;
  size_t best_i = 0;
  for (size_t i = 0; i < count; ++i) {
    const float d2 =
        detail::distanceSqKm(lat0, lon0, cos_lat, list[i].lat, list[i].lon);
    if (i == 0 || d2 < best_sq) {
      best_sq = d2;
      best_i = i;
    }
  }

  const Aircraft& a = list[best_i];
  best.valid = true;
  std::strncpy(best.callsign, a.callsign, sizeof(best.callsign) - 1);
  best.callsign[sizeof(best.callsign) - 1] = '\0';
  std::strncpy(best.type, a.type, sizeof(best.type) - 1);
  best.type[sizeof(best.type) - 1] = '\0';
  std::strncpy(best.alt, a.alt, sizeof(best.alt) - 1);
  best.alt[sizeof(best.alt) - 1] = '\0';
  best.distance_km = sqrtf(best_sq);
  best.gs_knots = a.gs_knots;
  best.track_deg = a.track_deg;
  return best;
}

}  // namespace services::adsb
