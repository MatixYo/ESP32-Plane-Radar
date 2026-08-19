#include "core/settings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "core/airport_find.h"
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
constexpr char kKeyTerrain[] = "showTerr";
constexpr char kKeySites[] = "sites";
constexpr char kKeySiteIdx[] = "siteIdx";

constexpr uint8_t kDefaultRangeIndex = 1;  // 40 NM ring

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_km = false;  // default is nautical miles
bool s_show_runways = true;
bool s_show_terrain = true;

char s_site_idents[kMaxSites][5] = {};
size_t s_site_count = 0;
uint8_t s_site_index = 0;

using KV = platform::KeyValueStore;

void airportToDegrees(const data::large_airports::Airport& ap, double* lat,
                      double* lon) {
  *lat = static_cast<double>(ap.lat_e7) / 1.0e7;
  *lon = static_cast<double>(ap.lon_e7) / 1.0e7;
}

void persistSitesString() {
  if (s_site_count == 0) {
    KV::remove(kNsRadar, kKeySites);
    return;
  }

  char buf[kMaxSites * 5];
  size_t at = 0;
  for (size_t i = 0; i < s_site_count; ++i) {
    if (i > 0) {
      buf[at++] = ',';
    }
    const size_t len = strlen(s_site_idents[i]);
    memcpy(buf + at, s_site_idents[i], len);
    at += len;
  }
  buf[at] = '\0';
  KV::putString(kNsRadar, kKeySites, buf);
}

void applyActiveSiteCoords() {
  if (s_site_count == 0) {
    return;
  }
  if (s_site_index >= s_site_count) {
    s_site_index = 0;
  }

  data::large_airports::Airport ap{};
  if (!core::airport::findAirport(s_site_idents[s_site_index], &ap)) {
    return;
  }
  airportToDegrees(ap, &s_lat, &s_lon);
}

void loadSitesFromStorage() {
  s_site_count = 0;
  s_site_index = 0;

  const std::string stored = KV::getString(kNsRadar, kKeySites, "");
  if (stored.empty()) {
    return;
  }

  char ident[5];
  size_t slot = 0;
  size_t i = 0;
  while (i <= stored.size() && slot < kMaxSites) {
    while (i < stored.size() &&
           (stored[i] == ',' || stored[i] == ' ' || stored[i] == '\t')) {
      ++i;
    }
    if (i >= stored.size()) {
      break;
    }

    size_t start = i;
    while (i < stored.size() && stored[i] != ',') {
      ++i;
    }
    const size_t len = i - start;
    if (len == 0 || len >= sizeof(ident)) {
      continue;
    }
    memcpy(ident, stored.data() + start, len);
    ident[len] = '\0';

    data::large_airports::Airport ap{};
    if (!core::airport::findAirport(ident, &ap)) {
      platform::logf("Unknown airport ident dropped: %s\n", ident);
      continue;
    }

    memcpy(s_site_idents[slot], ap.ident, sizeof(s_site_idents[slot]));
    ++slot;
  }

  s_site_count = slot;
  if (s_site_count == 0) {
    KV::remove(kNsRadar, kKeySiteIdx);
    return;
  }

  const uint8_t saved = KV::getU8(kNsRadar, kKeySiteIdx, 0);
  s_site_index = (saved < s_site_count) ? saved : 0;
  persistSitesString();
  KV::putU8(kNsRadar, kKeySiteIdx, s_site_index);
}

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
  s_show_terrain = KV::getBool(kNsRadar, kKeyTerrain, true);

  loadSitesFromStorage();
  applyActiveSiteCoords();
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
  if (s_site_count == 0) {
    s_lat = lat_v;
    s_lon = lon_v;
  }

  platform::logf("Radar location saved: %.6f, %.6f\n", lat_v, lon_v);
  return true;
}

void clearLocation() {
  KV::remove(kNsLocation, kKeyLat);
  KV::remove(kNsLocation, kKeyLon);
  KV::remove(kNsRadar, kKeySites);
  KV::remove(kNsRadar, kKeySiteIdx);
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
  s_site_count = 0;
  s_site_index = 0;
  for (size_t i = 0; i < kMaxSites; ++i) {
    s_site_idents[i][0] = '\0';
  }
}

// --- Airport site list -------------------------------------------------------

size_t siteCount() { return s_site_count; }

const char* siteIdent(size_t index) {
  if (index >= s_site_count) {
    return nullptr;
  }
  return s_site_idents[index];
}

const char* siteSlotIdent(size_t slot) {
  if (slot >= s_site_count) {
    return "";
  }
  return s_site_idents[slot];
}

const char* siteActiveIdent() {
  if (s_site_count == 0) {
    return nullptr;
  }
  return s_site_idents[s_site_index];
}

uint8_t siteIndex() { return s_site_index; }

void siteNext() {
  if (s_site_count < 2) {
    return;
  }
  s_site_index = static_cast<uint8_t>((s_site_index + 1) % s_site_count);
  KV::putU8(kNsRadar, kKeySiteIdx, s_site_index);
  applyActiveSiteCoords();
}

bool saveSites(const char* const* idents, size_t count) {
  char resolved[kMaxSites][5];
  size_t n = 0;

  for (size_t i = 0; i < count && n < kMaxSites; ++i) {
    if (idents[i] == nullptr || idents[i][0] == '\0') {
      continue;
    }

    data::large_airports::Airport ap{};
    if (!core::airport::findAirport(idents[i], &ap)) {
      platform::logf("Unknown airport ident ignored: %s\n", idents[i]);
      continue;
    }

    memcpy(resolved[n], ap.ident, sizeof(resolved[n]));
    ++n;
  }

  s_site_count = n;
  s_site_index = (s_site_index < s_site_count) ? s_site_index : 0;
  for (size_t i = 0; i < kMaxSites; ++i) {
    if (i < n) {
      memcpy(s_site_idents[i], resolved[i], sizeof(s_site_idents[i]));
    } else {
      s_site_idents[i][0] = '\0';
    }
  }

  persistSitesString();
  if (s_site_count == 0) {
    KV::remove(kNsRadar, kKeySiteIdx);
    if (KV::has(kNsLocation, kKeyLat) && KV::has(kNsLocation, kKeyLon)) {
      s_lat = KV::getDouble(kNsLocation, kKeyLat, config::kDefaultRadarLat);
      s_lon = KV::getDouble(kNsLocation, kKeyLon, config::kDefaultRadarLon);
    } else {
      s_lat = config::kDefaultRadarLat;
      s_lon = config::kDefaultRadarLon;
    }
    return true;
  }

  KV::putU8(kNsRadar, kKeySiteIdx, s_site_index);
  applyActiveSiteCoords();
  platform::logf("Airport sites saved: %u active\n",
                   static_cast<unsigned>(s_site_count));
  return true;
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

bool showTerrain() { return s_show_terrain; }

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

void saveTerrainFromPortal(const char* checkbox_value) {
  s_show_terrain = portalCheckboxChecked(checkbox_value);
  KV::putBool(kNsRadar, kKeyTerrain, s_show_terrain);
  platform::logf("Terrain layer: %s\n", s_show_terrain ? "on" : "off");
}

void unitsReset() {
  s_use_km = false;
  s_show_runways = true;
  s_show_terrain = true;
  KV::remove(kNsRadar, kKeyKm);
  KV::remove(kNsRadar, kKeyRunways);
  KV::remove(kNsRadar, kKeyTerrain);
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
