#include "services/airport_cache.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#include <cstdio>
#include <cstring>

namespace services::airport_cache {

namespace {

constexpr size_t kRamCacheSize = 24;
constexpr size_t kIcaoLen = 5;
constexpr size_t kIataLen = 5;
constexpr size_t kNameLen = 28;
constexpr char kDir[] = "/airports";

struct Entry {
  char icao[kIcaoLen];
  char iata[kIataLen];
  char name[kNameLen];
  bool valid;
};

Entry s_ram[kRamCacheSize];
bool s_ready = false;

void copyField(char* out, size_t out_len, const char* src) {
  if (out == nullptr || out_len == 0) {
    return;
  }
  if (src == nullptr) {
    out[0] = '\0';
    return;
  }
  strncpy(out, src, out_len - 1);
  out[out_len - 1] = '\0';
}

bool icaoOk(const char* icao) {
  return icao != nullptr && icao[0] != '\0' && strnlen(icao, kIcaoLen) < kIcaoLen;
}

void pathForIcao(const char* icao, char* path, size_t path_len) {
  snprintf(path, path_len, "%s/%s", kDir, icao);
}

bool ramLookup(const char* icao, char* iata_out, size_t iata_len, char* name_out,
               size_t name_len) {
  for (size_t i = 0; i < kRamCacheSize; ++i) {
    if (!s_ram[i].valid) {
      continue;
    }
    if (strcasecmp(s_ram[i].icao, icao) != 0) {
      continue;
    }
    copyField(iata_out, iata_len, s_ram[i].iata);
    copyField(name_out, name_len, s_ram[i].name);
    return true;
  }
  return false;
}

void ramStore(const char* icao, const char* iata, const char* name) {
  for (size_t i = 0; i < kRamCacheSize; ++i) {
    if (s_ram[i].valid && strcasecmp(s_ram[i].icao, icao) == 0) {
      copyField(s_ram[i].iata, sizeof(s_ram[i].iata), iata);
      copyField(s_ram[i].name, sizeof(s_ram[i].name), name);
      return;
    }
  }
  for (size_t i = 0; i < kRamCacheSize; ++i) {
    if (!s_ram[i].valid) {
      copyField(s_ram[i].icao, sizeof(s_ram[i].icao), icao);
      copyField(s_ram[i].iata, sizeof(s_ram[i].iata), iata);
      copyField(s_ram[i].name, sizeof(s_ram[i].name), name);
      s_ram[i].valid = true;
      return;
    }
  }
  // Full: overwrite slot 0 (simple; airports are stable).
  copyField(s_ram[0].icao, sizeof(s_ram[0].icao), icao);
  copyField(s_ram[0].iata, sizeof(s_ram[0].iata), iata);
  copyField(s_ram[0].name, sizeof(s_ram[0].name), name);
  s_ram[0].valid = true;
}

bool fsLookup(const char* icao, char* iata_out, size_t iata_len, char* name_out,
              size_t name_len) {
  if (!s_ready) {
    return false;
  }
  char path[32];
  pathForIcao(icao, path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) {
    return false;
  }
  char line[64];
  const size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
  f.close();
  if (n == 0) {
    return false;
  }
  line[n] = '\0';

  // Format: IATA|Name
  char* sep = strchr(line, '|');
  if (sep == nullptr) {
    return false;
  }
  *sep = '\0';
  copyField(iata_out, iata_len, line);
  copyField(name_out, name_len, sep + 1);
  return true;
}

void fsStore(const char* icao, const char* iata, const char* name) {
  if (!s_ready) {
    return;
  }
  if (!LittleFS.exists(kDir)) {
    LittleFS.mkdir(kDir);
  }
  char path[32];
  pathForIcao(icao, path, sizeof(path));
  File f = LittleFS.open(path, "w");
  if (!f) {
    Serial.printf("airport_cache: write failed %s\n", path);
    return;
  }
  f.printf("%s|%s\n", iata != nullptr ? iata : "", name != nullptr ? name : "");
  f.close();
}

}  // namespace

bool init() {
  for (size_t i = 0; i < kRamCacheSize; ++i) {
    s_ram[i].valid = false;
  }
  // Format if mount fails (first boot / empty partition).
  s_ready = LittleFS.begin(true, "/littlefs", 10, "spiffs");
  if (!s_ready) {
    Serial.println("airport_cache: LittleFS mount failed");
    return false;
  }
  if (!LittleFS.exists(kDir)) {
    LittleFS.mkdir(kDir);
  }
  Serial.println("airport_cache: LittleFS ready");
  return true;
}

bool lookup(const char* icao, char* iata_out, size_t iata_len, char* name_out,
            size_t name_len) {
  if (!icaoOk(icao)) {
    return false;
  }
  if (ramLookup(icao, iata_out, iata_len, name_out, name_len)) {
    return true;
  }

  char iata_tmp[kIataLen] = {};
  char name_tmp[kNameLen] = {};
  if (!fsLookup(icao, iata_tmp, sizeof(iata_tmp), name_tmp, sizeof(name_tmp))) {
    return false;
  }
  ramStore(icao, iata_tmp, name_tmp);
  copyField(iata_out, iata_len, iata_tmp);
  copyField(name_out, name_len, name_tmp);
  return true;
}

void store(const char* icao, const char* iata, const char* name) {
  if (!icaoOk(icao)) {
    return;
  }
  ramStore(icao, iata, name);
  fsStore(icao, iata, name);
}

}  // namespace services::airport_cache
