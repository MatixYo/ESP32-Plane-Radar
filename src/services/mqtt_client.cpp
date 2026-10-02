#include "services/mqtt_client.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "services/adsb_client.h"
#include "services/mqtt_config.h"
#include "services/mqtt_discovery.h"
#include "services/nearest_aircraft.h"
#include "services/radar_location.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"

namespace services::mqtt {

namespace {

enum class State : uint8_t { Disabled, WaitingLink, Connecting, Discovery, Ready };

constexpr size_t kNodeIdLen = 48;  // configurable base topic, slashes flattened
constexpr size_t kBaseTopicLen = 56;
constexpr size_t kClientIdLen = 64;
// Payload buffer: the host test caps every discovery payload at 600 B, and
// MQTT_MAX_PACKET_SIZE (768) must still fit topic (~52) + header.
constexpr size_t kPayloadMax = 640;
constexpr size_t kCmdMax = 64;

MqttConfig s_cfg{};
WiFiClient s_net;
PubSubClient s_mqtt(s_net);
State s_state = State::Disabled;
char s_node[kNodeIdLen] = {};
char s_base[kBaseTopicLen] = {};
char s_client_id[kClientIdLen] = {};
char s_topic[128] = {};
Ctx s_ctx{};
size_t s_discovery_index = 0;
unsigned long s_last_discovery_ms = 0;
unsigned long s_last_state_ms = 0;
unsigned long s_last_connect_ms = 0;
unsigned long s_discovery_done_ms = 0;
unsigned long s_last_skip_log_ms = 0;

// One buffer, allocated at boot and never freed. Both alternatives were
// measured on the bench board and both cost the ADS-B fetch its TLS handshake:
// a 3.3 KB aircraft snapshot held in .bss permanently (fixed 4.7 KB deficit),
// and the same snapshot malloc'd per publish (no fixed cost, but the
// malloc/free churn fragmented the heap so badly that the handshake lost its
// block ~50 s in). The fetch task caches the nearest aircraft instead, so
// nothing here needs the snapshot at all.
char s_payload[kPayloadMax];

void buildTopics() {
  // macSuffix() (not the raw low bits): the low 24 bits of the eFuse MAC are the
  // OUI, so two boards from one batch would share the base topic and the client
  // id. See macSuffix() for the measured case.
  const uint32_t mac6 = macSuffix(ESP.getEfuseMac());

  // Base topic: user-configured, else the firmware default.
  if (s_cfg.topic[0] != '\0') {
    std::strncpy(s_base, s_cfg.topic, sizeof(s_base) - 1);
    s_base[sizeof(s_base) - 1] = '\0';
  } else {
    std::snprintf(s_base, sizeof(s_base), "%s/%06x",
                  config::kMqttDefaultTopicPrefix, static_cast<unsigned>(mac6));
  }

  // Node id flattens the separators: it goes into discovery topics and unique
  // ids, where a slash would create phantom topic segments.
  size_t i = 0;
  for (; s_base[i] != '\0' && i + 1 < sizeof(s_node); ++i) {
    s_node[i] = (s_base[i] == '/') ? '_' : s_base[i];
  }
  s_node[i] = '\0';

  // Client id stays unique per chip even when two boards are cloned with the
  // same configured topic: a shared client id makes them kick each other off.
  std::snprintf(s_client_id, sizeof(s_client_id), "%s_%06x", s_node,
                static_cast<unsigned>(mac6));

  s_ctx.node = s_node;
  s_ctx.base = s_base;
  s_ctx.sw = config::kMqttSwVersion;
  s_ctx.prefix = s_cfg.prefix[0] != '\0' ? s_cfg.prefix
                                         : config::kMqttDefaultDiscoveryPrefix;
  s_ctx.dev_name = s_cfg.name[0] != '\0' ? s_cfg.name : config::kMqttDefaultDeviceName;
}

void publishStateTopic(const char* key, const char* value, bool retained) {
  std::snprintf(s_topic, sizeof(s_topic), "%s/state/%s", s_base, key);
  // QoS 0: fire and forget, no ack wait inside the render loop.
  if (!s_mqtt.publish(s_topic, value, retained)) {
    Serial.printf("MQTT: publish %s failed (heap %u)\n", s_topic,
                  static_cast<unsigned>(ESP.getFreeHeap()));
  }
}

void publishRangeState() {
  char label[16];
  std::snprintf(label, sizeof(label), "%d km",
                static_cast<int>(lroundf(ui::radar::rangeCurrent().ring3_km)));
  publishStateTopic("range", label, false);
}

void publishSwitchState(const char* key, bool on) {
  publishStateTopic(key, on ? "ON" : "OFF", false);
}

void publishLocationState() {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%.6f", services::location::lat());
  publishStateTopic("lat", buf, false);
  std::snprintf(buf, sizeof(buf), "%.6f", services::location::lon());
  publishStateTopic("lon", buf, false);
}

/** Every controllable/reported value once, so HA holds real states instead of
 *  'unknown' before the first command. */
void publishAllStates() {
  publishRangeState();
  publishSwitchState("miles", ui::radar::useMiles());
  publishSwitchState("runways", ui::radar::showRunways());
  publishLocationState();
}

void publishTelemetry() {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%u",
                static_cast<unsigned>(adsb::aircraftCount()));
  publishStateTopic("ac_count", buf, false);

  std::snprintf(buf, sizeof(buf), "%d", WiFi.RSSI());
  publishStateTopic("rssi", buf, false);
  std::snprintf(buf, sizeof(buf), "%u",
                static_cast<unsigned>(ESP.getFreeHeap()));
  publishStateTopic("heap", buf, false);

  // Cached by the fetch task while it held the parsed list: no allocation, and
  // no torn read of the whole aircraft list.
  const adsb::NearestAircraft nearest = adsb::nearest();
  if (nearestPayload(nearest, s_payload, kPayloadMax) > 0) {
    std::snprintf(s_topic, sizeof(s_topic), "%s/state/nearest", s_base);
    if (!s_mqtt.publish(s_topic, s_payload, false)) {
      Serial.println("MQTT: nearest publish failed");
    }
  }

  // Diagnostic snapshot. Note the temporary Strings: both outlive the full
  // expression, which is all infoPayload() needs.
  const String ip = WiFi.localIP().toString();
  const String ssid = WiFi.SSID();
  if (infoPayload(ip.c_str(), ssid.c_str(), millis() / 1000UL,
                  config::kMqttSwVersion, s_payload, kPayloadMax) > 0) {
    std::snprintf(s_topic, sizeof(s_topic), "%s/state/info", s_base);
    if (!s_mqtt.publish(s_topic, s_payload, false)) {
      Serial.println("MQTT: info publish failed");
    }
  }
}

void handleCommand(char* topic, const char* value) {
  const char* key = std::strrchr(topic, '/');
  if (key == nullptr) return;
  ++key;

  if (std::strcmp(key, "range") == 0) {
    const int km = leadingKm(value);
    for (size_t i = 0; i < ui::radar::kRangePresetCount; ++i) {
      if (static_cast<int>(lroundf(ui::radar::kRangePresets[i].ring3_km)) == km) {
        ui::radar::rangeSetIndex(static_cast<uint8_t>(i));
        break;
      }
    }
    publishRangeState();
  } else if (std::strcmp(key, "miles") == 0) {
    ui::radar::setUseMiles(payloadIsOn(value));
    publishSwitchState("miles", ui::radar::useMiles());
  } else if (std::strcmp(key, "runways") == 0) {
    ui::radar::setShowRunways(payloadIsOn(value));
    publishSwitchState("runways", ui::radar::showRunways());
  } else if (std::strcmp(key, "lat") == 0 || std::strcmp(key, "lon") == 0) {
    const double requested = std::atof(value);
    char lat_buf[24];
    char lon_buf[24];
    if (std::strcmp(key, "lat") == 0) {
      std::snprintf(lat_buf, sizeof(lat_buf), "%.6f", requested);
      std::snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lon());
    } else {
      std::snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::lat());
      std::snprintf(lon_buf, sizeof(lon_buf), "%.6f", requested);
    }
    if (!services::location::saveFromStrings(lat_buf, lon_buf)) {
      Serial.println("MQTT: rejected out-of-range coordinates");
    }
    // Re-publish the truth, not the request: an invalid value must not show
    // as applied in Home Assistant.
    publishLocationState();
  } else {
    Serial.printf("MQTT: unknown command topic %s\n", topic);
    return;
  }

  // Applied commands are worth a line: the only other feedback is the panel,
  // which is exactly what a remote caller cannot see.
  Serial.printf("MQTT: cmd %s -> %s\n", topic, value);

  // Grid geometry depends on range/units/location: full redraw, same call the
  // BOOT-button path uses. Safe here: loop() is the render task.
  if (WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void onMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (length >= kCmdMax) {
    length = kCmdMax - 1;
  }
  // PubSubClient hands over a NON NUL-terminated buffer.
  char value[kCmdMax];
  std::memcpy(value, payload, length);
  value[length] = '\0';
  handleCommand(topic, value);
}

/** Publishes one entity's discovery. False means "try me again later": the
 *  caller must NOT advance the index, or that entity is missing until reboot. */
bool publishDiscoveryStep() {
  const auto entity = static_cast<Entity>(s_discovery_index);
  if (discoveryTopic(entity, s_ctx, s_topic, sizeof(s_topic)) == 0) {
    return false;
  }
  const size_t n = discoveryPayload(entity, s_ctx, s_payload, kPayloadMax);
  if (n == 0) {
    Serial.printf("MQTT: discovery payload %u too large\n",
                  static_cast<unsigned>(s_discovery_index));
    return false;
  }
  const bool ok = s_mqtt.publish(s_topic, s_payload, true);  // retained
  Serial.printf("MQTT: discovery %u/%u %s\n",
                static_cast<unsigned>(s_discovery_index + 1),
                static_cast<unsigned>(Entity::Count), ok ? "ok" : "FAILED");
  return ok;
}

void connectBroker() {
  std::snprintf(s_topic, sizeof(s_topic), "%s/status", s_base);
  // cleanSession=false on purpose: a dropped link must not cost us the
  // subscription, and with a clean session the broker throws away any QoS 1
  // command published while we are away -- measured as silently dropped switch
  // taps in Home Assistant.
  const bool ok = s_mqtt.connect(s_client_id, s_cfg.user, s_cfg.pass, s_topic, 0,
                                 true, "offline", false);
  if (!ok) {
    Serial.printf("MQTT: connect failed, rc=%d\n", s_mqtt.state());
    return;
  }
  s_mqtt.publish(s_topic, "online", true);
  std::snprintf(s_topic, sizeof(s_topic), "%s/cmd/#", s_base);
  // QoS 1: a command published while the link is down must wait in the broker
  // instead of being dropped, and a QoS 0 one would. PubSubClient acks the
  // inbound QoS 1 publish.
  s_mqtt.subscribe(s_topic, 1);
  Serial.printf("MQTT: connected to %s:%u as %s (base '%s')\n", s_cfg.host, s_cfg.port,
                s_client_id, s_base);

  // Reconnecting after the fetch borrowed the socket must not replay the
  // discovery burst: it is retained in the broker and HA already has the
  // entities. A periodic refresh is cheap insurance against a broker that
  // restarted and lost the retained topics.
  const bool discovery_fresh =
      s_discovery_done_ms != 0 &&
      (millis() - s_discovery_done_ms) < config::kMqttDiscoveryRefreshMs;
  if (discovery_fresh) {
    publishAllStates();
    // Deliberately NOT resetting s_last_state_ms here: the fetch reconnects every
    // few seconds, so touching it each time meant the telemetry interval never
    // elapsed and every sensor silently froze at its last value.
    s_state = State::Ready;
    Serial.println("MQTT: reconnected (discovery still retained)");
    return;
  }
  s_discovery_index = 0;
  s_last_discovery_ms = 0;
  s_state = State::Discovery;
}

}  // namespace

// The MQTT packet ceiling lives in platformio.ini; if the two drift, discovery
// payloads get truncated silently at run time.
static_assert(MQTT_MAX_PACKET_SIZE == config::kMqttPacketSize,
              "MQTT_MAX_PACKET_SIZE must match config::kMqttPacketSize");

void init() {
  // init() runs again on every portal save, so it cannot assume it is booting: the
  // socket Home Assistant is watching is still open, and loop() is about to stop
  // driving it. A clean DISCONNECT suppresses the will and an undriven session never
  // loses its socket, so say goodbye on the old base before anything changes.
  if (s_mqtt.connected()) {
    std::snprintf(s_topic, sizeof(s_topic), "%s/status", s_base);
    s_mqtt.publish(s_topic, "offline", true);
    s_mqtt.disconnect();
  } else {
    s_net.stop();  // a socket left over from a failed reconnect, if any
  }
  loadConfig(s_cfg);
  buildTopics();
  if (!s_cfg.enabled) {
    s_state = State::Disabled;
    Serial.println("MQTT: disabled (portal checkbox off)");
    return;
  }
  if (!configured(s_cfg)) {
    s_state = State::Disabled;
    Serial.println("MQTT: enabled but no broker host configured");
    return;
  }
  s_net.setTimeout(3000);  // bounds the TCP connect; the default can hang for ~15 s
  s_mqtt.setServer(s_cfg.host, s_cfg.port);
  s_mqtt.setCallback(onMessage);
  s_mqtt.setBufferSize(config::kMqttPacketSize);
  // A saved config can rename the base topic, the discovery prefix or the device, so
  // the retained configs HA is holding no longer match what we are about to publish.
  // Reset the burst instead of waiting out kMqttDiscoveryRefreshMs with no entities.
  // Boot already starts at zero; only a portal save can arrive with these set.
  s_discovery_done_ms = 0;
  s_discovery_index = 0;
  s_last_discovery_ms = 0;
  s_state = State::WaitingLink;
  Serial.printf("MQTT: node %s client %s broker %s:%u topic '%s' prefix '%s'\n", s_node,
                s_client_id, s_cfg.host, s_cfg.port, s_base, s_ctx.prefix);
}

void loop() {
  if (s_state == State::Disabled) {
    return;
  }
  // Never drive the client while the fetch is in flight: they share the Wi-Fi
  // stack, and the fetch's TLS handshake is what needs the heap.
  if (services::adsb::fetchInProgress()) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (s_mqtt.connected()) {
      s_mqtt.disconnect();
    }
    s_state = State::WaitingLink;
    return;
  }

  if (!s_mqtt.connected()) {
    if (s_state == State::Discovery || s_state == State::Ready) {
      s_state = State::Connecting;  // broker dropped us
    }
    if (millis() - s_last_connect_ms < config::kMqttReconnectIntervalMs) {
      return;
    }
    s_last_connect_ms = millis();
    connectBroker();
    return;
  }

  s_mqtt.loop();

  if (s_state == State::Connecting || s_state == State::WaitingLink) {
    s_state = State::Discovery;  // connectBroker() already moved us; safety net
    s_discovery_index = 0;
  }

  if (s_state == State::Discovery) {
    if (millis() - s_last_discovery_ms < config::kMqttDiscoverySpacingMs) {
      return;
    }
    s_last_discovery_ms = millis();
    if (s_discovery_index < static_cast<size_t>(Entity::Count)) {
      // A step that could not allocate its buffers must not consume its index:
      // advancing would leave that entity unpublished until the next boot.
      if (publishDiscoveryStep()) {
        ++s_discovery_index;
      }
      return;
    }
    publishAllStates();
    s_last_state_ms = millis();
    s_discovery_done_ms = millis();
    s_state = State::Ready;
    Serial.println("MQTT: discovery complete");
    return;
  }

  if (millis() - s_last_state_ms >= config::kMqttStateIntervalMs) {
    if (ESP.getFreeHeap() < config::kMqttMinFreeHeap) {
      // A publish burst here would race the TLS handshake for the heap. Do not
      // start the interval again: MQTT is only up part of each fetch cycle, so
      // retry on the next pass (the log stays rate-limited).
      if (millis() - s_last_skip_log_ms >= config::kMqttStateIntervalMs) {
        s_last_skip_log_ms = millis();
        Serial.printf("MQTT: telemetry skipped, heap %u\n",
                      static_cast<unsigned>(ESP.getFreeHeap()));
      }
      return;
    }
    s_last_state_ms = millis();
    publishTelemetry();
  }
}

}  // namespace services::mqtt
