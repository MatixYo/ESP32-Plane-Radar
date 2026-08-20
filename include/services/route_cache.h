#pragma once

#include <cstddef>
#include <cstdint>

namespace services::route_cache {

/** Max cached routes (fits easily in ESP32 RAM). */
constexpr size_t kCacheSize = 50;
/** Cache TTL: 10 minutes (planes don't change route mid-flight). */
constexpr unsigned long kCacheTtlMs = 600000UL;

/** Origin + destination for one callsign (IATA / ICAO / short name). */
struct RouteInfo {
  char origin_iata[5];
  char origin_icao[5];
  char origin_name[28];
  char dest_iata[5];
  char dest_icao[5];
  char dest_name[28];
};

inline void clearRouteInfo(RouteInfo* info) {
  if (info == nullptr) {
    return;
  }
  info->origin_iata[0] = '\0';
  info->origin_icao[0] = '\0';
  info->origin_name[0] = '\0';
  info->dest_iata[0] = '\0';
  info->dest_icao[0] = '\0';
  info->dest_name[0] = '\0';
}

inline bool routeHasDisplay(const RouteInfo& info) {
  return info.origin_iata[0] != '\0' || info.origin_name[0] != '\0' ||
         info.dest_iata[0] != '\0' || info.dest_name[0] != '\0';
}

/** True if callsign is cached (including known-empty / negative). */
bool contains(const char* callsign);

/**
 * Look up a route by callsign.
 * Returns true if an unexpired entry exists (even when airports are empty).
 */
bool lookup(const char* callsign, RouteInfo* out);

/** Store (or refresh) a route. Empty airports = negative cache. */
void store(const char* callsign, const RouteInfo& info);

void purgeStale();
size_t count();

}  // namespace services::route_cache
