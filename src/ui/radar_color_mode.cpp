#include "ui/radar_color_mode.h"

#include <Preferences.h>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "planeradar";
constexpr char kPrefsColorModeKey[] = "colorMode";

Preferences s_prefs;
ColorMode s_color_mode = kColorModeNight;

void saveColorMode() {
  if (!s_prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  s_prefs.putUChar(kPrefsColorModeKey,
                   static_cast<uint8_t>(s_color_mode));
  s_prefs.end();
}

}  // namespace

void colorModeInit() {
  if (!s_prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  const uint8_t saved = s_prefs.getUChar(kPrefsColorModeKey, 0);
  s_color_mode =
      (saved == kColorModeDay) ? kColorModeDay : kColorModeNight;
  s_prefs.end();
}

ColorMode colorMode() { return s_color_mode; }

bool isDayMode() { return s_color_mode == kColorModeDay; }

void colorModeToggle() {
  s_color_mode = isDayMode() ? kColorModeNight : kColorModeDay;
  saveColorMode();
  Serial.printf("Color mode: %s\n", isDayMode() ? "day" : "night");
}

}  // namespace ui::radar
