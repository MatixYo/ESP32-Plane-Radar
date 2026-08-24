#include "core/airport_find.h"

#include <cctype>
#include <cstring>

namespace core::airport {

namespace {

using data::large_airports::Airport;
using data::large_airports::kAirportCount;
using data::large_airports::kAirports;

bool normalizeIcao(const char* icao, char out[5]) {
  if (icao == nullptr) {
    return false;
  }
  size_t len = strnlen(icao, 5);
  if (len != 4) {
    return false;
  }
  for (size_t i = 0; i < 4; ++i) {
    const unsigned char c = static_cast<unsigned char>(icao[i]);
    if (!std::isalpha(c)) {
      return false;
    }
    out[i] = static_cast<char>(std::toupper(c));
  }
  out[4] = '\0';
  return true;
}

int identCompare(const char* a, const char* b) { return strcmp(a, b); }

}  // namespace

bool findAirport(const char* icao, Airport* out) {
  char key[5];
  if (!normalizeIcao(icao, key)) {
    return false;
  }

  size_t lo = 0;
  size_t hi = kAirportCount;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = identCompare(kAirports[mid].ident, key);
    if (cmp == 0) {
      if (out != nullptr) {
        *out = kAirports[mid];
      }
      return true;
    }
    if (cmp < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return false;
}

}  // namespace core::airport
