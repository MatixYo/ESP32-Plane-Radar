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
constexpr char kPrefsAirlineKey[] = "airlnDisp";
constexpr char kPrefsDialogKey[] = "dlgFields";
constexpr char kPrefsDialogScaleKey[] = "dlgScale";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km ring
constexpr float kKmPerMile = 1.609344f;
constexpr float kDialogScaleMin = 0.5f;
constexpr float kDialogScaleMax = 3.0f;

constexpr uint16_t kDialogFieldsAll =
    (1u << static_cast<uint8_t>(DialogField::kCount)) - 1u;

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;
bool s_use_miles = false;
bool s_show_runways = true;
AirlineDisplay s_airline_display = AirlineDisplay::kNone;
uint16_t s_dialog_fields = kDialogFieldsAll;  // all details on by default
float s_dialog_text_scale = 1.0f;

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

void saveAirlineDisplay() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsAirlineKey, static_cast<uint8_t>(s_airline_display));
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
  const uint8_t airline_mode = s_prefs.getUChar(
      kPrefsAirlineKey, static_cast<uint8_t>(AirlineDisplay::kNone));
  s_airline_display = (airline_mode <= static_cast<uint8_t>(AirlineDisplay::kAbbrev))
                          ? static_cast<AirlineDisplay>(airline_mode)
                          : AirlineDisplay::kNone;
  s_dialog_fields = s_prefs.getUShort(kPrefsDialogKey, kDialogFieldsAll);
  s_dialog_text_scale = s_prefs.getFloat(kPrefsDialogScaleKey, 1.0f);
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

AirlineDisplay airlineDisplay() { return s_airline_display; }

bool dialogFieldEnabled(DialogField field) {
  return (s_dialog_fields & (1u << static_cast<uint8_t>(field))) != 0;
}

uint16_t dialogFieldsMask() { return s_dialog_fields; }

void setDialogFieldsMask(uint16_t mask) {
  s_dialog_fields = mask & kDialogFieldsAll;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.putUShort(kPrefsDialogKey, s_dialog_fields);
    s_prefs.end();
  }
}

const char* dialogFieldId(DialogField field) {
  switch (field) {
    case DialogField::kAirline: return "dlg_airline";
    case DialogField::kType: return "dlg_type";
    case DialogField::kAltitude: return "dlg_alt";
    case DialogField::kSpeed: return "dlg_speed";
    case DialogField::kTrack: return "dlg_track";
    case DialogField::kDistance: return "dlg_dist";
    case DialogField::kPosition: return "dlg_pos";
    default: return "";
  }
}

const char* dialogFieldLabel(DialogField field) {
  switch (field) {
    case DialogField::kAirline: return "Airline";
    case DialogField::kType: return "Aircraft type";
    case DialogField::kAltitude: return "Altitude";
    case DialogField::kSpeed: return "Ground speed";
    case DialogField::kTrack: return "Track";
    case DialogField::kDistance: return "Distance";
    case DialogField::kPosition: return "Position";
    default: return "";
  }
}

float dialogTextScale() { return s_dialog_text_scale; }

void saveDialogTextScaleFromPortal(const char* value) {
  float scale = 1.0f;
  if (value != nullptr && value[0] != '\0') {
    const float v = strtof(value, nullptr);
    if (v >= kDialogScaleMin && v <= kDialogScaleMax) {
      scale = v;
    }
  }
  s_dialog_text_scale = scale;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.putFloat(kPrefsDialogScaleKey, s_dialog_text_scale);
    s_prefs.end();
  }
  Serial.printf("Dialog text scale: %.2f\n", s_dialog_text_scale);
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

void saveAirlineDisplayFromPortal(const char* select_value) {
  uint8_t mode = static_cast<uint8_t>(AirlineDisplay::kNone);
  if (select_value != nullptr && select_value[0] != '\0') {
    const long v = strtol(select_value, nullptr, 10);
    if (v >= 0 && v <= static_cast<long>(AirlineDisplay::kAbbrev)) {
      mode = static_cast<uint8_t>(v);
    }
  }
  s_airline_display = static_cast<AirlineDisplay>(mode);
  saveAirlineDisplay();
  const char* labels[] = {"none", "full name", "abbreviation"};
  Serial.printf("Airline display: %s\n", labels[mode]);
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
  s_airline_display = AirlineDisplay::kNone;
  s_dialog_fields = kDialogFieldsAll;
  s_dialog_text_scale = 1.0f;
  if (s_prefs.begin(kPrefsNamespace, false)) {
    s_prefs.remove(kPrefsMilesKey);
    s_prefs.remove(kPrefsRunwaysKey);
    s_prefs.remove(kPrefsAirlineKey);
    s_prefs.remove(kPrefsDialogKey);
    s_prefs.remove(kPrefsDialogScaleKey);
    s_prefs.end();
  }
}

}  // namespace ui::radar
