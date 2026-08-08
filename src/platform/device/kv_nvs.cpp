/**
 * Device KeyValueStore: ESP32 NVS via Preferences.
 *
 * A local Preferences per call, opened and closed around each operation. This
 * mirrors what radar_location.cpp already did and avoids the NVS handle
 * conflicts documented in wifi_setup.cpp, which are why the "radar",
 * "planeradar" and "wifi" namespaces are kept separate in the first place.
 */

#include "core/platform.h"

#include <Preferences.h>

namespace core::platform {

namespace {

/** RAII open/close so no path can leak a handle. */
class Scoped {
 public:
  Scoped(const char* ns, bool read_only) {
    ok_ = prefs_.begin(ns, read_only);
  }
  ~Scoped() {
    if (ok_) {
      prefs_.end();
    }
  }

  bool ok() const { return ok_; }
  Preferences& operator*() { return prefs_; }

 private:
  Preferences prefs_;
  bool ok_ = false;
};

}  // namespace

bool KeyValueStore::has(const char* ns, const char* key) {
  Scoped p(ns, true);
  return p.ok() && (*p).isKey(key);
}

void KeyValueStore::remove(const char* ns, const char* key) {
  Scoped p(ns, false);
  if (p.ok()) {
    (*p).remove(key);
  }
}

bool KeyValueStore::getBool(const char* ns, const char* key, bool def) {
  Scoped p(ns, true);
  return p.ok() ? (*p).getBool(key, def) : def;
}

void KeyValueStore::putBool(const char* ns, const char* key, bool value) {
  Scoped p(ns, false);
  if (p.ok()) {
    (*p).putBool(key, value);
  }
}

uint8_t KeyValueStore::getU8(const char* ns, const char* key, uint8_t def) {
  Scoped p(ns, true);
  return p.ok() ? (*p).getUChar(key, def) : def;
}

void KeyValueStore::putU8(const char* ns, const char* key, uint8_t value) {
  Scoped p(ns, false);
  if (p.ok()) {
    (*p).putUChar(key, value);
  }
}

double KeyValueStore::getDouble(const char* ns, const char* key, double def) {
  Scoped p(ns, true);
  return p.ok() ? (*p).getDouble(key, def) : def;
}

void KeyValueStore::putDouble(const char* ns, const char* key, double value) {
  Scoped p(ns, false);
  if (p.ok()) {
    (*p).putDouble(key, value);
  }
}

std::string KeyValueStore::getString(const char* ns, const char* key,
                                     const char* def) {
  Scoped p(ns, true);
  if (!p.ok()) {
    return std::string(def);
  }
  return std::string((*p).getString(key, def).c_str());
}

void KeyValueStore::putString(const char* ns, const char* key,
                              const char* value) {
  Scoped p(ns, false);
  if (p.ok()) {
    (*p).putString(key, value);
  }
}

}  // namespace core::platform
