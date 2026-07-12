#pragma once

#include <cstddef>

namespace services::satellites {

struct Satellite {
  char name[25];
  float azimuth_deg;
  float elevation_deg;
  float range_km;
};

constexpr size_t kMaxVisible = 24;
constexpr size_t kMaxCatalog = 220;

size_t visibleCount();
const Satellite* visibleList();

using PollFn = void (*)();
void setPollFn(PollFn fn);

bool syncTime();

/** Download & cache the TLE group in RAM. Call at boot and every few hours. */
bool refreshTleCatalog();

/** Recompute az/el for all cached satellites — no network, fast, call often. */
bool recomputePositions(double observer_lat, double observer_lon, double observer_alt_m);

}  // namespace services::satellites
