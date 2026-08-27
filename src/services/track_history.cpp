#include "services/track_history.h"

#include <Arduino.h>

#include <cmath>
#include <cstring>

#include "config.h"
#include "services/radar_location.h"

namespace services::track {

namespace {

// Fixes are stored as int16 offsets from the current radar home, in units of
// 1e-4 deg (~11 m) — a quarter the RAM of a float lat/lon pair, and far finer
// than one screen pixel at any range preset. Home rarely moves; track::clear()
// is called when it does so stale offsets are not re-projected against a new
// origin.
constexpr float kScale = 10000.0f;  // deg -> stored unit

struct Fix {
  int16_t dlat;
  int16_t dlon;
};

struct Track {
  char hex[7] = {0};
  Fix pts[config::kTrackHistoryDepth];
  uint8_t count = 0;   // valid points, 0..kTrackHistoryDepth
  uint8_t head = 0;    // next write slot (ring buffer)
  unsigned long last_ms = 0;
  bool occupied = false;
};

Track s_tracks[config::kTrackHistoryMax];

int16_t encode(double deg) {
  const double u = deg * kScale;
  if (u >= 32767.0) return 32767;
  if (u <= -32768.0) return -32768;
  return static_cast<int16_t>(lround(u));
}

Track* find(const char* hex) {
  for (auto& t : s_tracks) {
    if (t.occupied && strcmp(t.hex, hex) == 0) {
      return &t;
    }
  }
  return nullptr;
}

// Free slot if any, else the least-recently-updated track.
Track* claim() {
  Track* victim = &s_tracks[0];
  for (auto& t : s_tracks) {
    if (!t.occupied) {
      return &t;
    }
    if (t.last_ms < victim->last_ms) {
      victim = &t;
    }
  }
  return victim;
}

void reset(Track* t) {
  t->occupied = false;
  t->hex[0] = '\0';
  t->count = 0;
  t->head = 0;
}

}  // namespace

void record(const char* hex, float lat, float lon) {
  if (hex == nullptr || hex[0] == '\0') {
    return;
  }

  const Fix fix{encode(lat - services::location::lat()),
                encode(lon - services::location::lon())};

  Track* t = find(hex);
  if (t == nullptr) {
    t = claim();
    reset(t);
    t->occupied = true;
    strncpy(t->hex, hex, sizeof(t->hex) - 1);
    t->hex[sizeof(t->hex) - 1] = '\0';
  }

  if (t->count > 0) {
    const uint8_t last =
        (t->head + config::kTrackHistoryDepth - 1) % config::kTrackHistoryDepth;
    const int ddlat = fix.dlat - t->pts[last].dlat;
    const int ddlon = fix.dlon - t->pts[last].dlon;
    const float min_step = config::kTrackHistoryMinStepDeg2 * kScale * kScale;
    if (static_cast<float>(ddlat) * ddlat + static_cast<float>(ddlon) * ddlon <
        min_step) {
      t->last_ms = millis();  // still alive, just not moving
      return;
    }
  }

  t->pts[t->head] = fix;
  t->head = static_cast<uint8_t>((t->head + 1) % config::kTrackHistoryDepth);
  if (t->count < config::kTrackHistoryDepth) {
    ++t->count;
  }
  t->last_ms = millis();
}

void expireStale() {
  const unsigned long now = millis();
  for (auto& t : s_tracks) {
    if (t.occupied && now - t.last_ms >= config::kTrackHistoryTtlMs) {
      reset(&t);
    }
  }
}

size_t path(const char* hex, const Point** out_points) {
  static Point ordered[config::kTrackHistoryDepth];
  *out_points = ordered;
  if (hex == nullptr || hex[0] == '\0') {
    return 0;
  }
  Track* t = find(hex);
  if (t == nullptr || t->count == 0) {
    return 0;
  }
  const double home_lat = services::location::lat();
  const double home_lon = services::location::lon();
  const uint8_t start = (t->head + config::kTrackHistoryDepth - t->count) %
                        config::kTrackHistoryDepth;
  for (uint8_t i = 0; i < t->count; ++i) {
    const Fix& f = t->pts[(start + i) % config::kTrackHistoryDepth];
    ordered[i].lat = static_cast<float>(home_lat + f.dlat / kScale);
    ordered[i].lon = static_cast<float>(home_lon + f.dlon / kScale);
  }
  return t->count;
}

void clear() {
  for (auto& t : s_tracks) {
    reset(&t);
  }
}

}  // namespace services::track
