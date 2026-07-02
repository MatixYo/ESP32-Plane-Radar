#include "hardware/board.h"

#include <Arduino.h>
#include <Preferences.h>

namespace hardware::board {

namespace {

constexpr char kPrefsNamespace[] = "board";
constexpr char kKeySel[] = "sel";

const char* const kNames[kBoardCount] = {
    "ESP32-C3 Super Mini",
    "ESP32-2424S012",
};

bool s_loaded = false;
Board s_active = compileDefault();

}  // namespace

const char* name(Board b) { return kNames[static_cast<uint8_t>(b)]; }

Board active() {
  if (s_loaded) {
    return s_active;
  }
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    if (prefs.isKey(kKeySel)) {
      const long v = prefs.getUChar(kKeySel, static_cast<uint8_t>(compileDefault()));
      if (isValidIndex(v)) {
        s_active = static_cast<Board>(v);
      }
    }
    prefs.end();
  }
  s_loaded = true;
  Serial.printf("[board] active: %s (%s)\n", name(s_active),
                s_active == compileDefault() ? "default" : "override");
  return s_active;
}

const DisplayPins& activePins() { return pins(active()); }

void setActive(Board b) {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putUChar(kKeySel, static_cast<uint8_t>(b));
    prefs.end();
  }
  s_active = b;
  s_loaded = true;
}

}  // namespace hardware::board
