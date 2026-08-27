#pragma once

#include <cstddef>

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  /** Vertical rate, ft/min (baro, else geometric). 0 = level or unknown. */
  float vert_rate_fpm;
  /** ICAO 24-bit address (lower-case hex) — stable id across polls for the trail. */
  char hex[7];
  char callsign[9];
  char type[5];
  char alt[12];
  /** Operating airline name (ASCII-folded) shown next to the type; "" when the
   *  callsign is not a scheduled flight. Filled by services::route. */
  char airline[24];
  /** Route endpoints for the tag: Italian city name when known, else the
   *  city's own name, else the IATA code, else the ICAO code, else "".
   *  Filled by services::route from the callsign. */
  char origin[20];
  char dest[20];
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
 * Fill origin/dest/airline on the current aircraft list from their callsigns.
 * Call *after* fetchUpdate() has returned, so its TLS session is already
 * torn down (the route lookup opens its own HTTPS connection and two live
 * WiFiClientSecure contexts exhaust the C3 heap).
 * Cached routes are free; unresolved callsigns cost up to two HTTPS GETs each
 * (hexdb route + adsbdb airline), capped at config::kRouteLookupsPerCycle
 * callsigns per call.
 */
void resolveRoutes();

}  // namespace services::adsb
