#pragma once

#include <cstddef>

namespace services::route {

/** Hook invoked during the HTTPS route lookup (cooperative networking). */
using PollFn = void (*)();

enum class Result {
  kFromCache,        // answer served from the in-RAM cache (no I/O)
  kFetched,          // a network lookup was performed this call
  kSkippedNoBudget,  // cache miss, but caller withheld the network
  kSkippedBadCallsign,  // not an airline callsign — never has a route
};

/**
 * Resolve the origin / destination endpoints and operating airline for
 * `callsign`.
 *
 * On success `origin` and `dest` are filled with the Italian city name when
 * known, otherwise the IATA code, otherwise "" (unknown); `airline` gets the
 * operator name (ASCII-folded) or "". A cached "no route" answer fills all
 * three with "" and reports kFromCache.
 *
 * At most one HTTPS GET is issued, and only when `allow_network` is true.
 */
Result resolve(const char* callsign, char* origin, size_t origin_len,
               char* dest, size_t dest_len, char* airline, size_t airline_len,
               PollFn poll, bool allow_network);

/** Lower-case + strip diacritics from a UTF-8 city name (exposed for tests). */
void normalizeCity(const char* in, char* out, size_t out_len);

}  // namespace services::route
