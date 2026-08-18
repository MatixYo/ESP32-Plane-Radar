#pragma once

/**
 * ADS-B client: fetch nearby traffic from adsb.fi and decode it.
 *
 * Networking goes through core::platform::HttpClient, so the device uses
 * Arduino's HTTPClient and the native harness uses libcurl.
 */

#include <cstddef>

#include "core/aircraft.h"
#include "core/platform.h"

namespace core::adsb {

size_t aircraftCount();
const Aircraft* aircraftList();

/**
 * Hook invoked during long HTTP I/O so the config portal and the BOOT button
 * stay responsive across a request. Wired to wifiLoop() in main.cpp.
 */
void setPollFn(platform::PollFn fn);

/** Drop cached aircraft (e.g. after switching radar centre). */
void clear();

/** Fetch aircraft within fetch_radius_km of the given centre. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

/** Decode an adsb.fi response body into the store. Exposed for testing. */
bool parseResponse(const char* json);

/** Build the adsb.fi request URL. Exposed for testing. */
void buildUrl(char* buf, size_t len, double center_lat, double center_lon,
              float fetch_radius_km);

}  // namespace core::adsb
