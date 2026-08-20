#pragma once

#include <cstddef>

namespace services::airport_cache {

/** Mount LittleFS. Call once from setup(). Returns false if mount fails. */
bool init();

/**
 * Look up an airport by ICAO (e.g. "EGNX").
 * Fills iata (e.g. "EMA") and/or name (e.g. "East Midlands") when non-null.
 * Checks RAM then LittleFS. Returns true if a cached entry exists.
 */
bool lookup(const char* icao, char* iata_out, size_t iata_len, char* name_out,
            size_t name_len);

/** Persist airport to RAM + LittleFS. */
void store(const char* icao, const char* iata, const char* name);

}  // namespace services::airport_cache
