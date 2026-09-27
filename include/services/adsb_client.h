#pragma once

#include <cstddef>

#include "data/airlines.h"

namespace services::adsb {

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char callsign[9];  // flight/callsign, e.g. "BAW123"
  char type[5];      // aircraft type code, e.g. "A320"
  char alt[12];
  // Resolved airline (nullptr if the callsign is not a known airline flight).
  // Provides the IATA acronym, full name, and friendly short name.
  const data::airlines::Airline* airline;
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
