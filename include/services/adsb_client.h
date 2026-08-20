#pragma once

#include <cstddef>
#include <cstdint>

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
  char dest[5];           // NEW: destination IATA code (e.g. "IBZ")
  unsigned long dest_fetched_ms;  // NEW: 0 = unknown / not fetched yet
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

/** Fetch route (origin/destination) for a given callsign from adsbdb.com.
 *  Fills dest_iata_out (len >= 5) on success. Returns true if known.
 *  Internally uses route_cache to avoid repeat requests.
 */
bool fetchRoute(const char* callsign, char* dest_iata_out, size_t dest_len);

}  // namespace services::adsb
