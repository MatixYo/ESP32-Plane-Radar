#include "ui/radar_range.h"

#include "ui/radar_theme.h"

#include <Preferences.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr char kPrefsHeadingKey[] = "headingTop";
constexpr uint8_t kDefaultRangeIndex = 2;  // 10 km ring
constexpr float kKmPerMile = 1.609344f;

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_show_runways = true;
uint16_t s_heading_at_top_deg = 0;

bool validHeadingAtTop(uint16_t heading) {
  return heading <= 359;
}

void saveRangeIndex() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsRangeKey, s_range_index);
  s_prefs.end();
}

void saveUseMiles() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsMilesKey, s_use_miles);
  s_prefs.end();
}

void saveShowRunways() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putBool(kPrefsRunwaysKey, s_show_runways);
  s_prefs.end();
}

void saveHeadingAtTop() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUShort(kPrefsHeadingKey, s_heading_at_top_deg);
  s_prefs.end();
}

bool portalCheckboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  // WiFiManager checkbox submits its value= attribute ("T", or "F" if we prefilled F).
  if ((value[0] == 'T' || value[0] == 't' || value[0] == 'F' || value[0] == 'f') &&
      value[1] == '\0') {
    return true;
  }
  return strcmp(value, "on") == 0;
}

}  // namespace

void rangeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
  s_use_miles = s_prefs.getBool(kPrefsMilesKey, false);
  s_show_runways = s_prefs.getBool(kPrefsRunwaysKey, true);
  const uint16_t saved_heading = s_prefs.getUShort(kPrefsHeadingKey, 0);
  s_heading_at_top_deg = validHeadingAtTop(saved_heading) ? saved_heading : 0;
  s_prefs.end();
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

bool saveRangeFromPortal(const char* range_km_value) {
  if (range_km_value == nullptr || range_km_value[0] == '\0') {
    return false;
  }

  char* end = nullptr;
  const float requested_km = strtof(range_km_value, &end);
  if (end == range_km_value || *end != '\0') {
    return false;
  }

  for (size_t i = 0; i < kRangePresetCount; ++i) {
    if (fabsf(requested_km - kRangePresets[i].ring3_km) < 0.01f) {
      s_range_index = static_cast<uint8_t>(i);
      saveRangeIndex();
      Serial.printf("Radar range: %.0f km\n", rangeCurrent().ring3_km);
      return true;
    }
  }
  return false;
}

float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px =
      static_cast<float>(kCenterX - kBeyondRingScreenMarginPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

uint16_t headingAtTopDeg() { return s_heading_at_top_deg; }

void rotateMapOffset(float east, float north, float* screen_east,
                     float* screen_north) {
  constexpr float kDegToRad = 0.01745329252f;
  const float angle = static_cast<float>(s_heading_at_top_deg) * kDegToRad;
  const float sin_a = sinf(angle);
  const float cos_a = cosf(angle);
  *screen_east = east * cos_a - north * sin_a;
  *screen_north = north * cos_a + east * sin_a;
}

float headingToScreen(float heading_deg) {
  float rotated = heading_deg - static_cast<float>(s_heading_at_top_deg);
  while (rotated < 0.0f) rotated += 360.0f;
  while (rotated >= 360.0f) rotated -= 360.0f;
  return rotated;
}

void saveMilesFromPortal(const char* checkbox_value) {
  s_use_miles = portalCheckboxChecked(checkbox_value);
  saveUseMiles();
  Serial.printf("Distance units: %s\n", s_use_miles ? "miles" : "km");
}

void saveRunwaysFromPortal(const char* checkbox_value) {
  s_show_runways = portalCheckboxChecked(checkbox_value);
  saveShowRunways();
  Serial.printf("Runway overlay: %s\n", s_show_runways ? "on" : "off");
}

bool saveHeadingFromPortal(const char* heading_deg_value) {
  if (heading_deg_value == nullptr || heading_deg_value[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const long heading = strtol(heading_deg_value, &end, 10);
  if (end == heading_deg_value || *end != '\0' || heading < 0 || heading > 359 ||
      !validHeadingAtTop(static_cast<uint16_t>(heading))) {
    return false;
  }
  s_heading_at_top_deg = static_cast<uint16_t>(heading);
  saveHeadingAtTop();
  Serial.printf("Heading at top: %u degrees\n", s_heading_at_top_deg);
  return true;
}

void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles) {
  if (use_miles) {
    const int mi = static_cast<int>(lroundf(ring3_km / kKmPerMile));
    snprintf(buf, len, "%dmi", mi);
  } else {
    const int km = static_cast<int>(lroundf(ring3_km));
    snprintf(buf, len, "%dkm", km);
  }
}

void formatCurrentRing3Label(char* buf, size_t len) {
  formatRing3Label(buf, len, rangeCurrent().ring3_km, s_use_miles);
}

void unitsReset() {
  s_use_miles = false;
  s_show_runways = true;
  s_heading_at_top_deg = 0;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsHeadingKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar
