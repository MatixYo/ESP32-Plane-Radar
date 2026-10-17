#include "ui/radar_range.h"

#include "ui/radar_theme.h"

#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr char kPrefsMilesKey[] = "useMiles";
constexpr char kPrefsRunwaysKey[] = "showRwys";
constexpr char kPrefsHeadingKey[] = "northDeg";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr float kKmPerMile = 1.609344f;
constexpr float kDegToRad = 0.01745329252f;

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_show_runways = true;
uint16_t s_heading_deg = 0;
float s_heading_sin = 0.0f;
float s_heading_cos = 1.0f;

void setHeading(uint16_t deg) {
  s_heading_deg = deg % 360;
  const float rad = static_cast<float>(s_heading_deg) * kDegToRad;
  s_heading_sin = sinf(rad);
  s_heading_cos = cosf(rad);
}

void saveHeading() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUShort(kPrefsHeadingKey, s_heading_deg);
  s_prefs.end();
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
  setHeading(s_prefs.getUShort(kPrefsHeadingKey, 0));
  s_prefs.end();
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

float fetchRadiusKm() {
  const float outer_km = rangeCurrent().outer_km;
  const float screen_r_px =
      static_cast<float>(kCenterX - kBeyondRingScreenMarginPx);
  return outer_km * (screen_r_px / static_cast<float>(kGridOuterRadius));
}

bool useMiles() { return s_use_miles; }

bool showRunways() { return s_show_runways; }

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

uint16_t headingDeg() { return s_heading_deg; }

void saveHeadingFromPortal(const char* value) {
  char* end = nullptr;
  const float parsed = (value != nullptr) ? strtof(value, &end) : 0.0f;
  if (end == nullptr || end == value || !std::isfinite(parsed)) {
    Serial.println("Invalid heading in portal — keeping previous value");
    return;
  }
  const long deg = lroundf(parsed);
  setHeading(static_cast<uint16_t>(((deg % 360) + 360) % 360));
  saveHeading();
  Serial.printf("Display heading: %u deg\n", s_heading_deg);
}

void rotateKmForHeading(float* dx_km, float* dy_km) {
  const float x = *dx_km;
  const float y = *dy_km;
  *dx_km = x * s_heading_cos - y * s_heading_sin;
  *dy_km = x * s_heading_sin + y * s_heading_cos;
}

float screenBearingDeg(float true_bearing_deg) {
  return true_bearing_deg - static_cast<float>(s_heading_deg);
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
  setHeading(0);
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsHeadingKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar
