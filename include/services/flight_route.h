#pragma once

#include <cstddef>

namespace services::route {

/** Hook invoked during the HTTPS route lookup (cooperative networking). */
using PollFn = void (*)();

enum class Result {
  kFromCache,        // answer served from the in-RAM cache (no I/O)
  kFetched,          // a network lookup was performed this call
  kSkippedNoBudget,  // cache miss, but caller withheld the network
  kSkippedBadCallsign,  // not an airline callsign — never has a route
};

/**
 * Resolve the origin / destination endpoints and operating airline for
 * `callsign`.
 *
 * The route comes from hexdb.io (callsign -> two ICAO codes), each resolved to
 * a city name locally via data::airports; the airline name comes from a second
 * GET to adsbdb. On success `airline` gets the operator name (ASCII-folded) or
 * "". `origin` / `dest` are filled per `full_names`: when true, the Italian
 * city name when known, otherwise the city's own name, otherwise the IATA
 * code, otherwise the ICAO code; when false, the raw IATA code (falling back
 * to ICAO when the airport has none) — i.e. the two lookups always happen
 * together and this only picks which label is copied out.
 *
 * `ac_lat` / `ac_lon` are the aircraft's current position: when both route
 * airports are in data::airports and the aircraft is more than
 * config::kRouteCorridorMaxKm outside the great-circle corridor between them,
 * the route is treated as a stale callsign match and `origin` / `dest` come
 * back "" (the airline still resolves). Pass 0/0 or NaN to skip that check.
 *
 * Up to two HTTPS GETs are issued, and only when `allow_network` is true.
 */
Result resolve(const char* callsign, char* origin, size_t origin_len,
               char* dest, size_t dest_len, char* airline, size_t airline_len,
               float ac_lat, float ac_lon, bool full_names, PollFn poll,
               bool allow_network);

/** Lower-case + strip diacritics from a UTF-8 city name (exposed for tests). */
void normalizeCity(const char* in, char* out, size_t out_len);

}  // namespace services::route
