#pragma once

#include <cstddef>
#include <cstdint>

#include "services/route_cache.h"

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
  char origin[5];          // Origin IATA (e.g. "BFS")
  char origin_icao[5];     // Origin ICAO (e.g. "EGAA")
  char origin_name[28];    // Short origin name (e.g. "Belfast")
  char dest[5];            // Destination IATA (e.g. "EMA")
  char dest_icao[5];       // Destination ICAO (e.g. "EGNX")
  char dest_name[28];      // Short dest name (e.g. "East Midlands")
  unsigned long route_fetched_ms;  // 0 = unknown / not fetched yet
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

/**
 * Resolve origin+destination via hexdb.io. Fills RouteInfo on success.
 * Uses route_cache + LittleFS airport_cache. Returns true if either airport
 * has displayable IATA/name.
 */
bool fetchRoute(const char* callsign, services::route_cache::RouteInfo* out);

}  // namespace services::adsb
