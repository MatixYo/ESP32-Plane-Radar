#pragma once

/**
 * Persistent user settings: radar centre, range preset, distance units and the
 * runway overlay toggle.
 *
 * Merged from the former services/radar_location.cpp and ui/radar_range.cpp,
 * which shared no code but the same job. Storage goes through
 * core::platform::KeyValueStore, so the device keeps using NVS and the native
 * harness uses a file.
 *
 * The two NVS namespaces are deliberately NOT merged — see kNsLocation /
 * kNsRadar below.
 */

#include <cstddef>
#include <cstdint>

namespace core::settings {

/**
 * Range presets (label on ring 3 = 3/4 of outer radius).
 *
 * Defined in nautical miles, the natural unit for aviation, and converted to km
 * because all the projection maths downstream is metric.
 *
 *  10 NM  — local area
 *  20 NM  — default; metro picture
 *  40 NM  — regional
 *  80 NM  — wide area; en-route traffic
 */
struct RangePreset {
  /** Distance shown on ring 3 (3/4 of outer radius), always stored in km. */
  float ring3_km;
  float outer_km;
};

constexpr float kRing3ToOuterKm = 4.0f / 3.0f;
constexpr float kKmPerNauticalMile = 1.852f;

/** Build a preset from a ring-3 distance in nautical miles. */
constexpr RangePreset presetFromNm(float ring3_nm) {
  return RangePreset{ring3_nm * kKmPerNauticalMile,
                     ring3_nm * kKmPerNauticalMile * kRing3ToOuterKm};
}

constexpr RangePreset kRangePresets[] = {
    presetFromNm(10.0f),
    presetFromNm(20.0f),
    presetFromNm(40.0f),
    presetFromNm(80.0f),
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

/**
 * Storage namespaces. These stay separate: on the device they are distinct NVS
 * namespaces, and wifi_setup.cpp documents that merging them risks NVS handle
 * conflicts. The key names are equally frozen — changing either would strand
 * every already-configured device on the next firmware update.
 */
constexpr char kNsLocation[] = "radar";     ///< keys: lat, lon
constexpr char kNsRadar[] = "planeradar";   ///< keys: rangeIdx, useKm, showRwys, sites, siteIdx

constexpr size_t kMaxSites = 6;

// --- Lifecycle ---------------------------------------------------------------

/** Load everything from storage, falling back to config defaults. */
void init();

// --- Radar centre ------------------------------------------------------------

double lat();
double lon();

/** Parse portal strings, validate, persist, and update the runtime values. */
bool saveLocationFromStrings(const char* lat_str, const char* lon_str);

/** Clear stored coordinates and revert to the config defaults. */
void clearLocation();

// --- Airport site list -------------------------------------------------------

size_t siteCount();
const char* siteIdent(size_t index);
const char* siteSlotIdent(size_t slot);
const char* siteActiveIdent();
uint8_t siteIndex();
void siteNext();
bool saveSites(const char* const* idents, size_t count);

// --- Range preset ------------------------------------------------------------

/** Advance to the next preset and persist the index. */
void rangeNext();
const RangePreset& rangeCurrent();
uint8_t rangeIndex();

// --- Units and overlays ------------------------------------------------------

/** False (the default) means nautical miles. */
bool useKm();
bool showRunways();

/** Apply a WiFi portal checkbox value and persist it. */
void saveKmFromPortal(const char* checkbox_value);
void saveRunwaysFromPortal(const char* checkbox_value);

/**
 * Reset units and the runway overlay to their defaults.
 *
 * Note the asymmetry with clearLocation(): this deliberately does NOT reset the
 * range preset. A Wi-Fi credential wipe returns the display to km and runways
 * on, but leaves the user's chosen zoom alone.
 */
void unitsReset();

// --- Pure helpers (exposed for unit testing) ---------------------------------

/** Strict strtod: rejects trailing garbage and empty input. */
bool parseCoord(const char* text, double* out);

/** Latitude within +/-90 and longitude within +/-180. */
bool validLatLon(double lat, double lon);

/**
 * Interpret a WiFiManager checkbox submission.
 *
 * WiFiManager submits the field's value= attribute rather than a conventional
 * "on", and the portal prefills that attribute with "T" (or "F"), encoding the
 * real state in the presence of the `checked` attribute. So any single T/F
 * means "the box was submitted", i.e. checked.
 */
bool portalCheckboxChecked(const char* value);

/** Render a ring-3 label, e.g. "40NM" or "74km". */
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_km);

/** formatRing3Label for the active preset and unit setting. */
void formatCurrentRing3Label(char* buf, size_t len);

}  // namespace core::settings
