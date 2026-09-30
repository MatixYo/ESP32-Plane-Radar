#pragma once

#include <cstddef>
#include <cstdint>

namespace services::mqtt {

constexpr size_t kBrokerHostLen = 64;
constexpr size_t kBrokerUserLen = 32;
constexpr size_t kBrokerPassLen = 96;
constexpr size_t kTopicLen = 40;
constexpr size_t kPrefixLen = 32;
constexpr size_t kDeviceNameLen = 40;

/** Everything the radar needs to reach a broker and announce itself in HA.
 *  Every text field stays EMPTY until the user types it in the config portal;
 *  the defaults live in config.h and are applied at use time, never pre-filled. */
struct MqttConfig {
  bool enabled;                        // portal checkbox
  char host[kBrokerHostLen + 1];       // broker IP or hostname
  uint16_t port;                       // empty field -> kMqttDefaultPort
  char user[kBrokerUserLen + 1];
  char pass[kBrokerPassLen + 1];
  char topic[kTopicLen + 1];           // base topic; empty -> planeradar/<mac6>
  char prefix[kPrefixLen + 1];         // discovery prefix; empty -> homeassistant
  char name[kDeviceNameLen + 1];       // HA device name; empty -> Plane Radar
};

/**
 * Default node suffix used when no base topic is configured: the unique half of
 * the eFuse MAC.
 *
 * ESP.getEfuseMac() packs mac[0] into the LOW byte, so the low 24 bits are the
 * OUI -- identical on every board from the same batch. Taking those gave two
 * radars the same base topic *and* the same MQTT client id (measured on the
 * bench: e8:3d:c1:84:b8:78 and e8:3d:c1:81:d0:00), and a shared client id makes
 * the two boards kick each other off the broker. The per-device bytes are
 * mac[3..5]; they are returned in MAC order so the suffix reads like the tail of
 * the address esptool prints.
 *
 * One expression on purpose: the firmware compiles these headers as C++11 (the
 * `-std=gnu++17` in platformio.ini does not reach the Arduino framework's TUs),
 * where a multi-statement constexpr body is rejected -- while the host test, built
 * with an explicit -std=c++17, accepts it happily. Keep header helpers one-liners.
 */
constexpr uint32_t macSuffix(uint64_t efuse_mac) {
  return (static_cast<uint32_t>((efuse_mac >> 24) & 0xFF) << 16) |
         (static_cast<uint32_t>((efuse_mac >> 32) & 0xFF) << 8) |
         static_cast<uint32_t>((efuse_mac >> 40) & 0xFF);
}

/** True when a broker host is filled in; without one there is nothing to reach. */
bool configured(const MqttConfig& cfg);

/** Load from nvs (ns "mqtt"); absent keys stay empty/default. */
void loadConfig(MqttConfig& out);

/** Persist; returns false when the nvs handle cannot be opened. */
bool saveConfig(const MqttConfig& cfg);

/** Drop every stored key (used by the BOOT long-press wipe). */
void clearConfig();

/** Apply the portal form values ("T"/"on" checkbox semantics) and persist. */
void saveFromPortal(const char* host, const char* port, const char* user,
                    const char* pass, const char* topic, const char* prefix,
                    const char* name, const char* enabled_checkbox);

}  // namespace services::mqtt
