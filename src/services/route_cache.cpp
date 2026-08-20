#include "services/route_cache.h"

#include <Arduino.h>
#include <cstring>

namespace services::route_cache {

namespace {

RouteEntry s_cache[kCacheSize];
size_t s_next_idx = 0;

}  // namespace

bool lookup(const char* callsign, char* dest_iata_out, size_t dest_len) {
  if (callsign == nullptr || callsign[0] == '\0' || dest_iata_out == nullptr || dest_len == 0) {
    return false;
  }

  const unsigned long now = millis();
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (!s_cache[i].valid) continue;
    if (strcasecmp(s_cache[i].callsign, callsign) == 0) {
      if (now - s_cache[i].fetched_ms > kCacheTtlMs) {
        s_cache[i].valid = false;
        return false;
      }
      strncpy(dest_iata_out, s_cache[i].dest_iata, dest_len - 1);
      dest_iata_out[dest_len - 1] = '\0';
      return true;
    }
  }
  return false;
}

void store(const char* callsign, const char* dest_iata) {
  if (callsign == nullptr || callsign[0] == '\0') return;

  // Update existing entry if found
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (s_cache[i].valid && strcasecmp(s_cache[i].callsign, callsign) == 0) {
      strncpy(s_cache[i].dest_iata, dest_iata, sizeof(s_cache[i].dest_iata) - 1);
      s_cache[i].dest_iata[sizeof(s_cache[i].dest_iata) - 1] = '\0';
      s_cache[i].fetched_ms = millis();
      return;
    }
  }

  // Try to find a free slot
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (!s_cache[i].valid) {
      strncpy(s_cache[i].callsign, callsign, sizeof(s_cache[i].callsign) - 1);
      s_cache[i].callsign[sizeof(s_cache[i].callsign) - 1] = '\0';
      strncpy(s_cache[i].dest_iata, dest_iata, sizeof(s_cache[i].dest_iata) - 1);
      s_cache[i].dest_iata[sizeof(s_cache[i].dest_iata) - 1] = '\0';
      s_cache[i].fetched_ms = millis();
      s_cache[i].valid = true;
      return;
    }
  }

  // Cache full: purge stale and try again once
  purgeStale();
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (!s_cache[i].valid) {
      strncpy(s_cache[i].callsign, callsign, sizeof(s_cache[i].callsign) - 1);
      s_cache[i].callsign[sizeof(s_cache[i].callsign) - 1] = '\0';
      strncpy(s_cache[i].dest_iata, dest_iata, sizeof(s_cache[i].dest_iata) - 1);
      s_cache[i].dest_iata[sizeof(s_cache[i].dest_iata) - 1] = '\0';
      s_cache[i].fetched_ms = millis();
      s_cache[i].valid = true;
      return;
    }
  }

  // Still full: overwrite oldest
  size_t oldest = 0;
  unsigned long oldest_age = 0;
  const unsigned long now = millis();
  for (size_t i = 0; i < kCacheSize; ++i) {
    unsigned long age = now - s_cache[i].fetched_ms;
    if (age > oldest_age) {
      oldest_age = age;
      oldest = i;
    }
  }
  strncpy(s_cache[oldest].callsign, callsign, sizeof(s_cache[oldest].callsign) - 1);
  s_cache[oldest].callsign[sizeof(s_cache[oldest].callsign) - 1] = '\0';
  strncpy(s_cache[oldest].dest_iata, dest_iata, sizeof(s_cache[oldest].dest_iata) - 1);
  s_cache[oldest].dest_iata[sizeof(s_cache[oldest].dest_iata) - 1] = '\0';
  s_cache[oldest].fetched_ms = now;
  s_cache[oldest].valid = true;
}

void purgeStale() {
  const unsigned long now = millis();
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (s_cache[i].valid && (now - s_cache[i].fetched_ms > kCacheTtlMs)) {
      s_cache[i].valid = false;
    }
  }
}

size_t count() {
  size_t n = 0;
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (s_cache[i].valid) ++n;
  }
  return n;
}

}  // namespace services::route_cache
