#pragma once

// Flight route lookup: maps a callsign to its departure/arrival airports via the
// adsbdb.com public API. Looked up on demand (when a flight dialog opens), not
// per aircraft. Routes exist only for scheduled flights.
namespace services::route {

struct RouteInfo {
  bool valid;
  char origin_code[5];   // IATA (falls back to ICAO)
  char origin_city[24];  // municipality, may be empty
  char dest_code[5];
  char dest_city[24];
};

/** Look up the route for a callsign. Returns true and fills out on success. */
bool lookup(const char* callsign, RouteInfo* out);

}  // namespace services::route
