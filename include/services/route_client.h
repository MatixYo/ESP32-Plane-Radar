#pragma once

#include <cstddef>

namespace services::route {

struct Airport {
  char code[5];
  char city[24];
};

struct Route {
  Airport origin;
  Airport destination;
};

void init();

bool lookup(const char* callsign, Route* out);

size_t fetchPending(double center_lat, double center_lon, float max_dist_km,
                    size_t max_lookups);

}  // namespace services::route
