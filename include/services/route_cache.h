#pragma once

#include <cstddef>
#include <cstdint>

namespace services::route_cache {

/** Max cached routes (fits easily in ESP32 RAM). */
constexpr size_t kCacheSize = 50;
/** Cache TTL: 10 minutes (planes don't change destination mid-flight). */
constexpr unsigned long kCacheTtlMs = 600000UL;

struct RouteEntry {
  char callsign[9];
  char dest_iata[5];   // e.g. "IBZ" + null
  unsigned long fetched_ms;
  bool valid;
};

/** Look up a route by callsign. Returns true + fills dest_iata if found & not expired. */
bool lookup(const char* callsign, char* dest_iata_out, size_t dest_len);

/** Store (or refresh) a route in the cache. */
void store(const char* callsign, const char* dest_iata);

/** Clear stale entries older than TTL. Call periodically or before store when full. */
void purgeStale();

/** Number of valid entries currently cached. */
size_t count();

}  // namespace services::route_cache
