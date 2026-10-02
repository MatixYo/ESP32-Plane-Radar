#include "services/mqtt_config.h"

#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::mqtt {

namespace {

constexpr char kPrefsNamespace[] = "mqtt";
constexpr char kKeyEnabled[] = "on";
constexpr char kKeyHost[] = "host";
constexpr char kKeyPort[] = "port";
constexpr char kKeyUser[] = "user";
constexpr char kKeyPass[] = "pass";
constexpr char kKeyTopic[] = "topic";
constexpr char kKeyPrefix[] = "prefix";
constexpr char kKeyName[] = "name";

void copyInto(char* dst, size_t dst_len, const char* src) {
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

/** Absent key must read as EMPTY (not as the previous default), so a cleared
 *  nvs really means "not configured". */
void readString(Preferences& prefs, const char* key, char* dst, size_t dst_len) {
  char tmp[128] = {};
  if (dst_len > sizeof(tmp)) {
    dst[0] = '\0';
    return;
  }
  const size_t got = prefs.getString(key, tmp, sizeof(tmp));
  copyInto(dst, dst_len, got > 0 ? tmp : "");
}

bool checkboxOn(const char* value) {
  if (value == nullptr || value[0] == '\0') return false;
  return std::strcmp(value, "on") == 0 || value[0] == 'T' || value[0] == 't';
}

}  // namespace

bool configured(const MqttConfig& cfg) { return cfg.host[0] != '\0'; }

void loadConfig(MqttConfig& out) {
  std::memset(&out, 0, sizeof(out));
  out.port = config::kMqttDefaultPort;

  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return;
  }
  out.enabled = prefs.getBool(kKeyEnabled, false);
  readString(prefs, kKeyHost, out.host, sizeof(out.host));
  out.port = prefs.getUShort(kKeyPort, config::kMqttDefaultPort);
  readString(prefs, kKeyUser, out.user, sizeof(out.user));
  readString(prefs, kKeyPass, out.pass, sizeof(out.pass));
  readString(prefs, kKeyTopic, out.topic, sizeof(out.topic));
  readString(prefs, kKeyPrefix, out.prefix, sizeof(out.prefix));
  readString(prefs, kKeyName, out.name, sizeof(out.name));
  prefs.end();
}

bool saveConfig(const MqttConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return false;
  }
  prefs.putBool(kKeyEnabled, cfg.enabled);
  prefs.putString(kKeyHost, cfg.host);
  prefs.putUShort(kKeyPort, cfg.port);
  prefs.putString(kKeyUser, cfg.user);
  prefs.putString(kKeyPass, cfg.pass);
  prefs.putString(kKeyTopic, cfg.topic);
  prefs.putString(kKeyPrefix, cfg.prefix);
  prefs.putString(kKeyName, cfg.name);
  prefs.end();
  return true;
}

void clearConfig() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.clear();
  prefs.end();
  Serial.println("MQTT: config cleared");
}

void saveFromPortal(const char* host, const char* port, const char* user,
                    const char* pass, const char* topic, const char* prefix,
                    const char* name, const char* enabled_checkbox) {
  MqttConfig cfg{};
  copyInto(cfg.host, sizeof(cfg.host), host);  // no default: empty stays empty
  const long parsed = (port != nullptr && port[0]) ? std::strtol(port, nullptr, 10) : 0;
  cfg.port = (parsed > 0 && parsed <= 65535) ? static_cast<uint16_t>(parsed)
                                            : config::kMqttDefaultPort;
  copyInto(cfg.user, sizeof(cfg.user), user);
  copyInto(cfg.pass, sizeof(cfg.pass), pass);
  copyInto(cfg.topic, sizeof(cfg.topic), topic);
  copyInto(cfg.prefix, sizeof(cfg.prefix), prefix);
  copyInto(cfg.name, sizeof(cfg.name), name);
  cfg.enabled = checkboxOn(enabled_checkbox);
  saveConfig(cfg);
  // Log what a connection needs; the password never reaches the log.
  Serial.printf("MQTT: %s host '%s' port %u user '%s' topic '%s'\n",
                cfg.enabled ? "enabled" : "disabled", cfg.host, cfg.port, cfg.user,
                cfg.topic[0] ? cfg.topic : "(auto)");
}

}  // namespace services::mqtt
