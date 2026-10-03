#include "services/airport_filters.h"

#include <Preferences.h>
#include <Arduino.h>

namespace services::airport_filters {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLargeAirports[] = "apt_large";
constexpr char kKeyMediumAirports[] = "apt_medium";
constexpr char kKeySmallAirports[] = "apt_small";
constexpr char kKeyMilitaryAirports[] = "apt_military";

static bool s_show_large = true;
static bool s_show_medium = true;
static bool s_show_small = false;
static bool s_show_military = false;

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLargeAirports)) {
    s_show_large = prefs.getBool(kKeyLargeAirports, true);
  }
  if (prefs.isKey(kKeyMediumAirports)) {
    s_show_medium = prefs.getBool(kKeyMediumAirports, true);
  }
  if (prefs.isKey(kKeySmallAirports)) {
    s_show_small = prefs.getBool(kKeySmallAirports, false);
  }
  if (prefs.isKey(kKeyMilitaryAirports)) {
    s_show_military = prefs.getBool(kKeyMilitaryAirports, false);
  }
  prefs.end();

  Serial.printf("airport_filters: large=%d medium=%d small=%d military=%d\n",
                s_show_large, s_show_medium, s_show_small, s_show_military);
}

bool showLargeAirports() { return s_show_large; }
bool showMediumAirports() { return s_show_medium; }
bool showSmallAirports() { return s_show_small; }
bool showMilitaryAirports() { return s_show_military; }

bool saveFromPortal(const char* large, const char* medium,
                    const char* small, const char* military) {
  s_show_large = (large && large[0] == 'T');
  s_show_medium = (medium && medium[0] == 'T');
  s_show_small = (small && small[0] == 'T');
  s_show_military = (military && military[0] == 'T');

  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.println("airport_filters: failed to open NVS for writing");
    return false;
  }

  prefs.putBool(kKeyLargeAirports, s_show_large);
  prefs.putBool(kKeyMediumAirports, s_show_medium);
  prefs.putBool(kKeySmallAirports, s_show_small);
  prefs.putBool(kKeyMilitaryAirports, s_show_military);
  prefs.end();

  Serial.printf("airport_filters saved: large=%d medium=%d small=%d military=%d\n",
                s_show_large, s_show_medium, s_show_small, s_show_military);
  return true;
}

void clear() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.remove(kKeyLargeAirports);
  prefs.remove(kKeyMediumAirports);
  prefs.remove(kKeySmallAirports);
  prefs.remove(kKeyMilitaryAirports);
  prefs.end();

  s_show_large = true;
  s_show_medium = true;
  s_show_small = false;
  s_show_military = false;

  Serial.println("airport_filters cleared (reset to defaults)");
}

bool shouldShowAirportType(uint8_t airport_type) {
  switch (airport_type) {
    case 0: return s_show_large;
    case 1: return s_show_medium;
    case 2: return s_show_small;
    case 3: return s_show_military;
    default: return true;
  }
}

}  // namespace services::airport_filters
