#include "services/time_settings.h"

#include <Preferences.h>

#include <cctype>
#include <cstring>
#include <time.h>

#include "config.h"

namespace services::time_settings {
namespace {

constexpr char kPrefsNamespace[] = "clock";
constexpr char kManualTimezoneKey[] = "manualTz";
constexpr char kTimezoneKey[] = "timezone";
constexpr char kClock24HourKey[] = "clock24";
constexpr size_t kTimezoneMaxLen = 63;

bool s_manual_timezone = false;
bool s_clock_24_hour = true;
char s_timezone[kTimezoneMaxLen + 1] = {};

bool checkboxChecked(const char* value) {
  return value != nullptr &&
         (strcmp(value, "T") == 0 || strcmp(value, "on") == 0);
}

bool validTimezone(const char* value) {
  if (value == nullptr || value[0] == '\0' ||
      strnlen(value, kTimezoneMaxLen + 1) > kTimezoneMaxLen) {
    return false;
  }
  for (const char* cursor = value; *cursor != '\0'; ++cursor) {
    const unsigned char character = static_cast<unsigned char>(*cursor);
    if (!(std::isalnum(character) || strchr("_+-,./<>", *cursor) != nullptr)) {
      return false;
    }
  }
  return true;
}

void applyTimezone() {
  setenv("TZ", s_timezone, 1);
  tzset();
}

void setAutomaticTimezone() {
  s_manual_timezone = false;
  strncpy(s_timezone, config::kLocalTimeZone, sizeof(s_timezone) - 1);
  s_timezone[sizeof(s_timezone) - 1] = '\0';
}

}  // namespace

void init() {
  setAutomaticTimezone();
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    s_clock_24_hour = prefs.getBool(kClock24HourKey, true);
    if (prefs.getBool(kManualTimezoneKey, false)) {
      const String saved_timezone = prefs.getString(kTimezoneKey, "");
      if (validTimezone(saved_timezone.c_str())) {
        s_manual_timezone = true;
        saved_timezone.toCharArray(s_timezone, sizeof(s_timezone));
      }
    }
    prefs.end();
  }
  applyTimezone();
}

const char* timeZone() { return s_timezone; }

bool usesManualTimeZone() { return s_manual_timezone; }

bool uses24HourClock() { return s_clock_24_hour; }

void saveFromPortal(const char* manual_timezone_value, const char* timezone_value,
                    const char* clock_24h_value) {
  const bool wants_manual_timezone = checkboxChecked(manual_timezone_value);
  if (wants_manual_timezone && validTimezone(timezone_value)) {
    s_manual_timezone = true;
    strncpy(s_timezone, timezone_value, sizeof(s_timezone) - 1);
    s_timezone[sizeof(s_timezone) - 1] = '\0';
  } else {
    setAutomaticTimezone();
    if (wants_manual_timezone) {
      Serial.println("Invalid manual timezone; using automatic timezone");
    }
  }
  s_clock_24_hour = checkboxChecked(clock_24h_value);

  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putBool(kManualTimezoneKey, s_manual_timezone);
    prefs.putString(kTimezoneKey, s_timezone);
    prefs.putBool(kClock24HourKey, s_clock_24_hour);
    prefs.end();
  }
  applyTimezone();
  Serial.printf("Clock settings: timezone=%s, format=%s\n", s_timezone,
                s_clock_24_hour ? "24h" : "12h");
}

}  // namespace services::time_settings