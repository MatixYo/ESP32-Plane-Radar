#pragma once

#include "core/large_airports.h"

namespace core::airport {

/** Look up a 4-letter ICAO code in the embedded large-airport table. */
bool findAirport(const char* icao, data::large_airports::Airport* out);

}  // namespace core::airport
