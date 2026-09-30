#pragma once

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>  // strcasecmp

#include "services/nearest_aircraft.h"
#include "ui/radar_range.h"  // kRangePresets: single source for the option list

namespace services::mqtt {

/** One discovery payload per controllable/reportable value. Order is the
 *  publish order used by the client state machine. */
enum class Entity : uint8_t {
  Range = 0,
  Miles,
  Runways,
  Latitude,
  Longitude,
  AircraftCount,
  Nearest,
  Rssi,
  HeapFree,
  Info,
  Count,
};

struct Ctx {
  /** Node id: "<dev>_<hex6>", used in uniq_id and dev.ids. */
  const char* node;
  /** Base topic (the "~" substitution): "planeradar/<hex6>". */
  const char* base;
  /** Firmware version advertised in dev.sw. */
  const char* sw;
  /** Discovery prefix, normally "homeassistant". */
  const char* prefix;
  /** Device name shown in HA (dev.name); defaults to Plane Radar. */
  const char* dev_name;
};

namespace detail {

constexpr const char* kDevMf = "MatixYo";
constexpr const char* kDevMdl = "ESP32-C3 SuperMini + GC9A01 240x240";
constexpr const char* kConfigUrl = "http://plane-radar.local/";

/** Sequential, bounds-checked JSON writer: one pass, no intermediate Strings. */
struct Writer {
  char* buf;
  size_t len;
  size_t n;

  void add(const char* s) {
    if (n >= len) return;
    const size_t need = std::strlen(s);
    const size_t room = len - 1 - n;
    const size_t take = need < room ? need : room;
    std::memcpy(buf + n, s, take);
    n += take;
    buf[n] = '\0';
    if (take < need) n = len;  // mark overflow
  }

  void addf(const char* fmt, ...) {
    if (n >= len) return;
    va_list ap;
    va_start(ap, fmt);
    const int written = std::vsnprintf(buf + n, len - n, fmt, ap);
    va_end(ap);
    if (written < 0 || static_cast<size_t>(written) >= len - n) {
      n = len;
      return;
    }
    n += static_cast<size_t>(written);
  }

  bool overflowed() const { return n >= len; }
};

struct Spec {
  const char* component;
  const char* obj;
  const char* name;  // JSON-safe: no quotes/backslashes in these literals
};

// Order MUST match enum class Entity.
constexpr Spec kSpecs[] = {
    {"select", "range", "Range"},
    {"switch", "miles", "Miles"},
    {"switch", "runways", "Runways"},
    {"number", "latitude", "Latitude"},
    {"number", "longitude", "Longitude"},
    {"sensor", "aircraft_count", "Aircraft in range"},
    {"sensor", "nearest", "Nearest aircraft"},
    {"sensor", "rssi", "Wi-Fi RSSI"},
    {"sensor", "heap_free", "Free heap"},
    {"sensor", "info", "Radar info"},
};

constexpr const char* kStateTopic[] = {
    "~/state/range",   "~/state/miles",    "~/state/runways",
    "~/state/lat",     "~/state/lon",      "~/state/ac_count",
    "~/state/nearest", "~/state/rssi",     "~/state/heap",
    "~/state/info"};

constexpr const char* kCmdTopic[] = {
    "~/cmd/range", "~/cmd/miles", "~/cmd/runways",
    "~/cmd/lat",   "~/cmd/lon",   nullptr,
    nullptr,       nullptr,       nullptr,
    nullptr};

/** Entity-specific keys, appended right after availability. No trailing comma. */
inline void addEntityData(Writer& w, Entity e) {
  switch (e) {
    case Entity::Range:
      w.add("\"options\":[");
      for (size_t i = 0; i < ui::radar::kRangePresetCount; ++i) {
        // Option labels come from the same table rangeIndex() indexes.
        w.addf("%s\"%d km\"", i ? "," : "",
               static_cast<int>(lroundf(ui::radar::kRangePresets[i].ring3_km)));
      }
      w.add("],\"ic\":\"mdi:radar\"");
      break;
    case Entity::Miles:
      w.add("\"ic\":\"mdi:tape-measure\",\"ent_cat\":\"config\"");
      break;
    case Entity::Runways:
      w.add("\"ic\":\"mdi:run\",\"ent_cat\":\"config\"");
      break;
    case Entity::Latitude:
      // HA rejects anything below 0.001 ("value must be at least 0.001 at
      // 'step'") and drops the whole entity, so this is the floor, not a
      // preference: 0.001 deg ~ 111 m, which is finer than the ring scale.
      w.add("\"min\":-90,\"max\":90,\"step\":0.001,\"mode\":\"box\","
            "\"ic\":\"mdi:latitude\",\"ent_cat\":\"config\"");
      break;
    case Entity::Longitude:
      w.add("\"min\":-180,\"max\":180,\"step\":0.001,\"mode\":\"box\","
            "\"ic\":\"mdi:longitude\",\"ent_cat\":\"config\"");
      break;
    case Entity::AircraftCount:
      w.add("\"stat_cla\":\"measurement\",\"sug_dsp_prc\":0,"
            "\"ic\":\"mdi:airplane-marker\"");
      break;
    case Entity::Nearest:
      w.add("\"json_attr_t\":\"~/state/nearest\","
            "\"val_t\":\"{{ value_json.callsign }}\",\"ic\":\"mdi:airplane\"");
      break;
    case Entity::Rssi:
      w.add("\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\","
            "\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\"");
      break;
    case Entity::HeapFree:
      w.add("\"unit_of_meas\":\"B\",\"stat_cla\":\"measurement\","
            "\"ent_cat\":\"diagnostic\"");
      break;
    case Entity::Info:
      w.add("\"json_attr_t\":\"~/state/info\","
            "\"val_t\":\"{{ value_json.ip }}\",\"ent_cat\":\"diagnostic\","
            "\"ic\":\"mdi:information-outline\"");
      break;
    case Entity::Count:
      break;
  }
}

inline void addJsonEscaped(Writer& w, const char* s) {
  for (; s && *s; ++s) {
    if (*s == '"' || *s == '\\') {
      char esc[3] = {'\\', *s, '\0'};
      w.add(esc);
    } else {
      char one[2] = {*s, '\0'};
      w.add(one);
    }
  }
}

}  // namespace detail

/** "<prefix>/<component>/<node>/<obj>/config". Returns bytes written, 0 when
 *  the buffer is too small. */
inline size_t discoveryTopic(Entity e, const Ctx& ctx, char* out, size_t out_len) {
  if (e >= Entity::Count || out_len == 0) return 0;
  const size_t i = static_cast<size_t>(e);
  const int n = std::snprintf(out, out_len, "%s/%s/%s/%s/config", ctx.prefix,
                              detail::kSpecs[i].component, ctx.node,
                              detail::kSpecs[i].obj);
  return (n < 0 || static_cast<size_t>(n) >= out_len) ? 0 : static_cast<size_t>(n);
}

/** Retained discovery JSON. Returns bytes written, 0 when the buffer is too
 *  small (never a truncated payload). */
inline size_t discoveryPayload(Entity e, const Ctx& ctx, char* out, size_t out_len) {
  if (e >= Entity::Count || out_len == 0) return 0;
  const size_t i = static_cast<size_t>(e);

  detail::Writer w{out, out_len, 0};
  out[0] = '\0';
  w.addf("{\"~\":\"%s\",\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"obj_id\":\"%s\",",
         ctx.base, detail::kSpecs[i].name, ctx.node, detail::kSpecs[i].obj,
         detail::kSpecs[i].obj);
  if (detail::kCmdTopic[i] != nullptr) {
    w.addf("\"stat_t\":\"%s\",\"cmd_t\":\"%s\",", detail::kStateTopic[i],
           detail::kCmdTopic[i]);
  } else {
    w.addf("\"stat_t\":\"%s\",", detail::kStateTopic[i]);
  }
  w.add("\"avty_t\":\"~/status\",\"pl_avail\":\"online\","
        "\"pl_not_avail\":\"offline\",");
  detail::addEntityData(w, e);
  w.addf(",\"dev\":{\"ids\":[\"%s\"],\"name\":\"", ctx.node);
  w.add(ctx.dev_name);
  w.addf("\",\"mf\":\"%s\",\"mdl\":\"%s\",\"sw\":\"%s\",\"cu\":\"%s\"}}",
         detail::kDevMf, detail::kDevMdl, ctx.sw, detail::kConfigUrl);

  return w.overflowed() ? 0 : w.n;
}

/** Telemetry JSON for the nearest aircraft; state and attributes come from
 *  this single document (stat_t == json_attr_t). */
inline size_t nearestPayload(const adsb::NearestAircraft& a, char* out, size_t out_len) {
  if (out_len == 0) return 0;
  detail::Writer w{out, out_len, 0};
  out[0] = '\0';
  w.add("{\"callsign\":\"");
  if (a.valid) {
    detail::addJsonEscaped(w, a.callsign);
  } else {
    w.add("none");
  }
  w.add("\",\"type\":\"");
  if (a.valid) detail::addJsonEscaped(w, a.type);
  w.add("\",\"alt\":\"");
  if (a.valid) detail::addJsonEscaped(w, a.alt);
  w.addf("\",\"distance_km\":%.1f,\"gs_kt\":%.0f,\"track_deg\":%.0f}",
         static_cast<double>(a.valid ? a.distance_km : 0.0f),
         static_cast<double>(a.valid ? a.gs_knots : 0.0f),
         static_cast<double>(a.valid ? a.track_deg : 0.0f));
  return w.overflowed() ? 0 : w.n;
}

/** Diagnostic snapshot: state is the IP, the rest rides as attributes. The
 *  availability topic stays online/offline -- a JSON there would mark every
 *  entity unavailable. */
inline size_t infoPayload(const char* ip, const char* ssid, unsigned long uptime_s,
                          const char* fw, char* out, size_t out_len) {
  if (out_len == 0) return 0;
  detail::Writer w{out, out_len, 0};
  out[0] = '\0';
  w.add("{\"ip\":\"");
  detail::addJsonEscaped(w, ip);
  w.add("\",\"ssid\":\"");
  detail::addJsonEscaped(w, ssid);
  w.addf("\",\"uptime_s\":%lu,\"fw\":\"", uptime_s);
  detail::addJsonEscaped(w, fw);
  w.add("\"}");
  return w.overflowed() ? 0 : w.n;
}

/** Leading integer of a command payload ("25 km" -> 25); -1 when absent. */
inline int leadingKm(const char* payload) {
  if (payload == nullptr) return -1;
  int value = 0;
  size_t digits = 0;
  for (const char* p = payload; *p; ++p) {
    if (*p == ' ') continue;
    if (*p < '0' || *p > '9') break;
    value = value * 10 + (*p - '0');
    ++digits;
  }
  return digits == 0 ? -1 : value;
}

/** True for ON/1/true/yes, case-insensitive. */
inline bool payloadIsOn(const char* payload) {
  if (payload == nullptr) return false;
  return strcasecmp(payload, "ON") == 0 || std::strcmp(payload, "1") == 0 ||
         strcasecmp(payload, "true") == 0 || strcasecmp(payload, "yes") == 0;
}

}  // namespace services::mqtt
