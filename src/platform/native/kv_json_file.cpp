/**
 * Native KeyValueStore: a JSON file, one object per NVS namespace.
 *
 * ~/.plane-radar/settings.json, or $PLANE_RADAR_SETTINGS. The layout mirrors
 * NVS one-for-one:
 *
 *   {"radar":{"lat":52.3676,"lon":4.9041},
 *    "planeradar":{"rangeIdx":1,"useKm":false,"showRwys":true}}
 *
 * The file is read once into a process-wide document and every put/remove
 * rewrites the whole thing immediately. That mirrors the device, where NVS
 * commits on Preferences::end() at the close of each call, so nothing can be
 * lost by quitting the harness right after a change. The rewrite goes through a
 * temporary file and rename() so an interrupted write cannot leave a truncated
 * settings.json behind.
 *
 * Pretty-printed on purpose: hand-editing this file to jump the harness to a
 * different airport is a first-class workflow, which is also why a malformed
 * file only warns and behaves as empty instead of aborting the program.
 */

#include "core/platform.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <ArduinoJson.h>

namespace core::platform {

namespace {

constexpr char kPathEnv[] = "PLANE_RADAR_SETTINGS";
constexpr char kDirName[] = ".plane-radar";
constexpr char kFileName[] = "settings.json";

/** Resolved once; the environment is not re-read mid-run. */
const std::string& path() {
  static const std::string resolved = [] {
    const char* from_env = std::getenv(kPathEnv);
    if (from_env != nullptr && from_env[0] != '\0') {
      return std::string(from_env);
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
      // Say so: settings written relative to the cwd will look like they
      // vanished the moment the harness is started from elsewhere.
      logf("kv: HOME unset, using ./%s/%s\n", kDirName, kFileName);
      return std::string(kDirName) + "/" + kFileName;
    }
    return std::string(home) + "/" + kDirName + "/" + kFileName;
  }();
  return resolved;
}

/** 0700: this is per-user state, and may hold a home location. */
void ensureParentDir() {
  const size_t slash = path().find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return;  // bare filename or a file at the root: nothing to create
  }
  const std::string dir = path().substr(0, slash);
  if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
    logf("kv: cannot create %s: %s\n", dir.c_str(), std::strerror(errno));
  }
}

/** Whole-file read. Returns false only for a real I/O problem, not absence. */
bool readFile(std::string* out) {
  std::FILE* f = std::fopen(path().c_str(), "rb");
  if (f == nullptr) {
    return false;  // first run has no file; the caller treats that as empty
  }
  char buf[1024];
  size_t got = 0;
  while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    out->append(buf, got);
  }
  std::fclose(f);
  return true;
}

JsonDocument loadFile() {
  JsonDocument doc;
  doc.to<JsonObject>();  // the top level is always the namespace map

  std::string text;
  if (!readFile(&text)) {
    return doc;
  }

  const DeserializationError err = deserializeJson(doc, text);
  if (err) {
    logf("kv: %s is not valid JSON (%s); starting empty\n", path().c_str(),
         err.c_str());
  } else if (!doc.is<JsonObject>()) {
    logf("kv: %s is not a JSON object; starting empty\n", path().c_str());
  } else {
    return doc;
  }
  // Either failure leaves the document in an unusable shape, and the next put
  // would otherwise try to add a member to a non-object.
  doc.to<JsonObject>();
  return doc;
}

/** The single in-memory copy. Loaded on first touch, never re-read. */
JsonDocument& doc() {
  static JsonDocument loaded = loadFile();
  return loaded;
}

void save() {
  ensureParentDir();

  std::string text;
  serializeJsonPretty(doc(), text);
  text += '\n';

  const std::string tmp = path() + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    logf("kv: cannot write %s: %s\n", tmp.c_str(), std::strerror(errno));
    return;
  }
  const bool written =
      std::fwrite(text.data(), 1, text.size(), f) == text.size();
  std::fflush(f);
  ::fsync(::fileno(f));  // the rename is only atomic if the bytes hit the disk
  std::fclose(f);

  if (!written || std::rename(tmp.c_str(), path().c_str()) != 0) {
    logf("kv: failed to update %s: %s\n", path().c_str(),
         std::strerror(errno));
    std::remove(tmp.c_str());
  }
}

/** Unbound (absent) when the namespace, the key, or the file is missing. */
JsonVariantConst find(const char* ns, const char* key) {
  return doc()[ns].as<JsonObjectConst>()[key];
}

template <typename T>
void put(const char* ns, const char* key, T value) {
  doc()[ns][key] = value;  // creates the namespace object on demand
  save();
}

}  // namespace

bool KeyValueStore::has(const char* ns, const char* key) {
  // is<JsonVariantConst>() is the v7 spelling of containsKey(): true for any
  // bound member, including one whose value is JSON null.
  return find(ns, key).is<JsonVariantConst>();
}

void KeyValueStore::remove(const char* ns, const char* key) {
  JsonObject obj = doc()[ns].as<JsonObject>();
  if (obj.isNull()) {
    return;  // no such namespace, so nothing changed and nothing to write
  }
  obj.remove(key);
  save();
}

bool KeyValueStore::getBool(const char* ns, const char* key, bool def) {
  const JsonVariantConst v = find(ns, key);
  return v.is<bool>() ? v.as<bool>() : def;
}

void KeyValueStore::putBool(const char* ns, const char* key, bool value) {
  put(ns, key, value);
}

uint8_t KeyValueStore::getU8(const char* ns, const char* key, uint8_t def) {
  // is<uint8_t>() is range-checked, so a hand-edited 300 falls back to def
  // rather than wrapping the way a raw cast would.
  const JsonVariantConst v = find(ns, key);
  return v.is<uint8_t>() ? v.as<uint8_t>() : def;
}

void KeyValueStore::putU8(const char* ns, const char* key, uint8_t value) {
  put(ns, key, value);
}

double KeyValueStore::getDouble(const char* ns, const char* key, double def) {
  // is<double>() accepts integers too: a hand-written "lat": 52 must work.
  const JsonVariantConst v = find(ns, key);
  return v.is<double>() ? v.as<double>() : def;
}

void KeyValueStore::putDouble(const char* ns, const char* key, double value) {
  put(ns, key, value);
}

std::string KeyValueStore::getString(const char* ns, const char* key,
                                     const char* def) {
  const JsonVariantConst v = find(ns, key);
  return v.is<const char*>() ? std::string(v.as<const char*>())
                             : std::string(def);
}

void KeyValueStore::putString(const char* ns, const char* key,
                              const char* value) {
  put(ns, key, value);
}

}  // namespace core::platform
