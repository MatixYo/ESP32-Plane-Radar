#include "services/route_cache.h"

#include <Arduino.h>
#include <cstring>

namespace services::route_cache {

namespace {

struct RouteEntry {
  char callsign[9];
  RouteInfo info;
  unsigned long fetched_ms;
  bool valid;
};

RouteEntry s_cache[kCacheSize];

void copyField(char* out, size_t out_len, const char* src) {
  if (out == nullptr || out_len == 0) {
    return;
  }
  if (src == nullptr) {
    out[0] = '\0';
    return;
  }
  strncpy(out, src, out_len - 1);
  out[out_len - 1] = '\0';
}

void copyRouteInfo(RouteInfo* dst, const RouteInfo& src) {
  if (dst == nullptr) {
    return;
  }
  *dst = src;
}

int findIndex(const char* callsign) {
  if (callsign == nullptr || callsign[0] == '\0') {
    return -1;
  }
  const unsigned long now = millis();
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (!s_cache[i].valid) {
      continue;
    }
    if (strcasecmp(s_cache[i].callsign, callsign) != 0) {
      continue;
    }
    if (now - s_cache[i].fetched_ms > kCacheTtlMs) {
      s_cache[i].valid = false;
      return -1;
    }
    return static_cast<int>(i);
  }
  return -1;
}

void writeEntry(RouteEntry* entry, const char* callsign, const RouteInfo& info) {
  copyField(entry->callsign, sizeof(entry->callsign), callsign);
  entry->info = info;
  entry->fetched_ms = millis();
  entry->valid = true;
}

}  // namespace

bool contains(const char* callsign) { return findIndex(callsign) >= 0; }

bool lookup(const char* callsign, RouteInfo* out) {
  const int idx = findIndex(callsign);
  if (idx < 0) {
    return false;
  }
  copyRouteInfo(out, s_cache[idx].info);
  return true;
}

void store(const char* callsign, const RouteInfo& info) {
  if (callsign == nullptr || callsign[0] == '\0') {
    return;
  }

  for (size_t i = 0; i < kCacheSize; ++i) {
    if (s_cache[i].valid && strcasecmp(s_cache[i].callsign, callsign) == 0) {
      writeEntry(&s_cache[i], callsign, info);
      return;
    }
  }

  for (size_t i = 0; i < kCacheSize; ++i) {
    if (!s_cache[i].valid) {
      writeEntry(&s_cache[i], callsign, info);
      return;
    }
  }

  purgeStale();
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (!s_cache[i].valid) {
      writeEntry(&s_cache[i], callsign, info);
      return;
    }
  }

  size_t oldest = 0;
  unsigned long oldest_age = 0;
  const unsigned long now = millis();
  for (size_t i = 0; i < kCacheSize; ++i) {
    const unsigned long age = now - s_cache[i].fetched_ms;
    if (age > oldest_age) {
      oldest_age = age;
      oldest = i;
    }
  }
  writeEntry(&s_cache[oldest], callsign, info);
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
    if (s_cache[i].valid) {
      ++n;
    }
  }
  return n;
}

}  // namespace services::route_cache
