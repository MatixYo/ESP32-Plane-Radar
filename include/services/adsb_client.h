#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];
  char type[5];
  char alt[12];
  char hex[7];           // ICAO hex code (e.g. "49D2E1")
  char reg[12];          // Tail registration (e.g. "OK-HEU")
  char desc[24];         // Model description / name
  char squawk[6];        // Squawk code (e.g. "7000")
  float baro_rate_fpm;   // Vertical climb/descent rate in ft/min
  bool on_ground;        // True if on ground
  float dist_km;         // Distance from radar center in km
  float bearing_deg;     // Bearing from radar center (0-360 deg)
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

}  // namespace services::adsb
