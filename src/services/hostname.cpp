#include "services/hostname.h"

#include <Preferences.h>
#include <cstdio>
#include <cstring>

#include "config.h"

namespace services::hostname {

namespace {

constexpr char kPrefsNamespace[] = "hostname";
constexpr char kKeyHost[] = "host";
constexpr size_t kMaxLen = 32;

char s_hostname[kMaxLen + 1];
char s_host_url[kMaxLen + 1 + 6];  // + ".local"

void updateHostUrl() {
  snprintf(s_host_url, sizeof(s_host_url), "%s.local", s_hostname);
}

void setRuntimeValue(const char* hostname) {
  strncpy(s_hostname, hostname, kMaxLen);
  s_hostname[kMaxLen] = '\0';
  updateHostUrl();
}

bool validHostname(const char* text) {
  const size_t len = text != nullptr ? strlen(text) : 0;
  if (len == 0 || len > kMaxLen) {
    return false;
  }
  if (text[0] == '-' || text[len - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const char c = text[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

void persist(const char* hostname) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString(kKeyHost, hostname);
  prefs.end();
  setRuntimeValue(hostname);
}

}  // namespace

void init() {
  setRuntimeValue(config::kPortalHostname);
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyHost)) {
    char stored[kMaxLen + 1];
    prefs.getString(kKeyHost, stored, sizeof(stored));
    if (validHostname(stored)) {
      setRuntimeValue(stored);
    }
  }
  prefs.end();
}

const char* value() { return s_hostname; }

const char* hostUrl() { return s_host_url; }

bool saveFromString(const char* hostname) {
  if (!validHostname(hostname)) {
    return false;
  }
  persist(hostname);
  Serial.printf("mDNS hostname saved: %s\n", s_hostname);
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyHost);
  prefs.end();
  setRuntimeValue(config::kPortalHostname);
}

}  // namespace services::hostname
