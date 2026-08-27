#pragma once

#include <cstddef>

namespace services::track {

struct Point {
  float lat;
  float lon;
};

/** Append the current fix for `hex` (ICAO address). No-op on an empty hex; a
 *  fix that barely moved from the last one is dropped so the ring keeps
 *  advancing. */
void record(const char* hex, float lat, float lon);

/** Forget any track with no new fix in the last config::kTrackHistoryTtlMs.
 *  Call once per ADS-B poll, after recording the fresh fixes. */
void expireStale();

/** Oldest -> newest fixes for `hex`. Returns the count (0 if unknown); the
 *  pointer is valid until the next path() call (shared static buffer). */
size_t path(const char* hex, const Point** out_points);

/** Drop all stored history. */
void clear();

}  // namespace services::track
