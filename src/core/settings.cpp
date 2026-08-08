#include "core/settings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "core/platform.h"

namespace core::settings {

namespace {

constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
constexpr char kKeyRange[] = "rangeIdx";
/**
 * Deliberately not the old "useMiles" key. That one meant km-vs-statute-miles;
 * this one means NM-vs-km, so reusing it would silently invert the preference
 * on every already-configured device.
 */
constexpr char kKeyKm[] = "useKm";
constexpr char kKeyRunways[] = "showRwys";

constexpr uint8_t kDefaultRangeIndex = 1;  // 40 NM ring

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_km = false;  // default is nautical miles
bool s_show_runways = true;

using KV = platform::KeyValueStore;

}  // namespace

// --- Lifecycle ---------------------------------------------------------------

void init() {
  if (KV::has(kNsLocation, kKeyLat) && KV::has(kNsLocation, kKeyLon)) {
    const double lat_v =
        KV::getDouble(kNsLocation, kKeyLat, config::kDefaultRadarLat);
    const double lon_v =
        KV::getDouble(kNsLocation, kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(lat_v, lon_v)) {
      s_lat = lat_v;
      s_lon = lon_v;
    }
  }

  const uint8_t saved = KV::getU8(kNsRadar, kKeyRange, kDefaultRangeIndex);
  s_range_index = (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_km = KV::getBool(kNsRadar, kKeyKm, false);
  s_show_runways = KV::getBool(kNsRadar, kKeyRunways, true);
}

// --- Radar centre ------------------------------------------------------------

double lat() { return s_lat; }

double lon() { return s_lon; }

bool saveLocationFromStrings(const char* lat_str, const char* lon_str) {
  double lat_v = 0.0;
  double lon_v = 0.0;
  if (!parseCoord(lat_str, &lat_v) || !parseCoord(lon_str, &lon_v)) {
    return false;
  }
  if (!validLatLon(lat_v, lon_v)) {
    return false;
  }

  KV::putDouble(kNsLocation, kKeyLat, lat_v);
  KV::putDouble(kNsLocation, kKeyLon, lon_v);
  s_lat = lat_v;
  s_lon = lon_v;

  platform::logf("Radar location saved: %.6f, %.6f\n", lat_v, lon_v);
  return true;
}

void clearLocation() {
  KV::remove(kNsLocation, kKeyLat);
  KV::remove(kNsLocation, kKeyLon);
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
}

// --- Range preset ------------------------------------------------------------

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  KV::putU8(kNsRadar, kKeyRange, s_range_index);
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

// --- Units and overlays ------------------------------------------------------

bool useKm() { return s_use_km; }

bool showRunways() { return s_show_runways; }

void saveKmFromPortal(const char* checkbox_value) {
  s_use_km = portalCheckboxChecked(checkbox_value);
  KV::putBool(kNsRadar, kKeyKm, s_use_km);
  platform::logf("Distance units: %s\n", s_use_km ? "km" : "NM");
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  s_show_runways = portalCheckboxChecked(checkbox_value);
  KV::putBool(kNsRadar, kKeyRunways, s_show_runways);
  platform::logf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

void unitsReset() {
  s_use_km = false;
  s_show_runways = true;
  KV::remove(kNsRadar, kKeyKm);
  KV::remove(kNsRadar, kKeyRunways);
  // rangeIdx is intentionally left alone; see the header.
}

// --- Pure helpers ------------------------------------------------------------

bool parseCoord(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) {
    return false;
  }
  *out = v;
  return true;
}

bool validLatLon(double lat_v, double lon_v) {
  return lat_v >= -90.0 && lat_v <= 90.0 && lon_v >= -180.0 && lon_v <= 180.0;
}

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' ||
       value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_km) {
  if (use_km) {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  } else {
    const int nm = static_cast<int>(lroundf(ring3_km / kKmPerNauticalMile));
    snprintf(buf, len, "%dNM", nm);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_km);
}

}  // namespace core::settings
