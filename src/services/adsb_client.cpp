#include "services/adsb_client.h"

#include <ESP.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

#include "config.h"
#include "services/airport_cache.h"
#include "services/route_cache.h"
#include "ui/radar_display.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr char kRouteApiBase[] = "https://hexdb.io/api/v1/route/icao/";
constexpr char kAirportApiBase[] = "https://hexdb.io/api/v1/airport/icao/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 10000;
constexpr unsigned long kRequestTimeoutMs = 15000;
constexpr int kHttpsAttempts = 2;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

void prepareHttpsHeap() {
  // 240×240 RGB565 sprite ≈ 112 KB — free it so mbedTLS can allocate.
  ui::radarDisplaySuspendFrameBuffer();
  Serial.printf("https: heap free=%u maxblk=%u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0) {
    payload.reserve(static_cast<unsigned>(content_length + 1));
  }

  uint8_t buffer[512];
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int available = stream->available();
    if (available > 0) {
      const int to_read =
          available > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer))
                                                       : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        payload.concat(reinterpret_cast<const char*>(buffer),
                       static_cast<unsigned>(read_bytes));
      }
    }
    if (content_length > 0 &&
        static_cast<int>(payload.length()) >= content_length) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return payload.length() > 0;
}

/**
 * One TLS session per attempt. Does not hammer GET() on a dead client —
 * that was spamming SSL -32512 (OOM) in the log.
 */
bool httpsGetPayload(const String& url, String& payload, int* http_code_out) {
  prepareHttpsHeap();
  payload = "";

  for (int attempt = 1; attempt <= kHttpsAttempts; ++attempt) {
    pollNetwork();
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, url)) {
      Serial.printf("https: begin failed (%s)\n", url.c_str());
      delay(200);
      continue;
    }
    http.setConnectTimeout(kConnectAttemptMs);
    http.setTimeout(kRequestTimeoutMs);
    http.setReuse(false);

    const int code = http.GET();
    if (http_code_out != nullptr) {
      *http_code_out = code;
    }

    if (code == HTTP_CODE_OK) {
      const bool ok = readResponseBodyWithPoll(http, payload);
      http.end();
      return ok;
    }

    Serial.printf("https: HTTP %d attempt %d/%d\n", code, attempt, kHttpsAttempts);
    http.end();
    delay(250 * attempt);
  }
  return false;
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || !obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

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

void shortenAirportName(const char* full, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (full == nullptr || full[0] == '\0') {
    return;
  }
  copyField(out, out_len, full);

  static const char* kSuffixes[] = {
      " International Airport",
      " International",
      " Airport",
      " Airfield",
      " Aerodrome",
  };
  for (const char* suffix : kSuffixes) {
    const size_t name_len = strlen(out);
    const size_t suffix_len = strlen(suffix);
    if (name_len > suffix_len &&
        strcasecmp(out + name_len - suffix_len, suffix) == 0) {
      out[name_len - suffix_len] = '\0';
      break;
    }
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));

  ac->origin[0] = '\0';
  ac->origin_icao[0] = '\0';
  ac->origin_name[0] = '\0';
  ac->dest[0] = '\0';
  ac->dest_icao[0] = '\0';
  ac->dest_name[0] = '\0';
  ac->route_fetched_ms = 0;
}

bool httpGetJson(const char* url, JsonDocument& doc) {
  String payload;
  int code = 0;
  if (!httpsGetPayload(url, payload, &code)) {
    Serial.printf("route: HTTP %d for %s\n", code, url);
    return false;
  }

  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("route: JSON error: %s\n", err.c_str());
    return false;
  }
  return true;
}

bool resolveAirport(const char* icao_code, char* iata_out, size_t iata_len,
                    char* name_out, size_t name_len) {
  if (icao_code == nullptr || icao_code[0] == '\0') {
    return false;
  }

  char iata[5] = {};
  char name[28] = {};
  if (services::airport_cache::lookup(icao_code, iata, sizeof(iata), name,
                                      sizeof(name))) {
    copyField(iata_out, iata_len, iata);
    copyField(name_out, name_len, name);
    return iata[0] != '\0' || name[0] != '\0';
  }

  String url = kAirportApiBase;
  url += icao_code;
  Serial.printf("route: airport fetch %s\n", url.c_str());

  JsonDocument doc;
  if (!httpGetJson(url.c_str(), doc)) {
    return false;
  }

  const char* api_iata = doc["iata"] | "";
  const char* api_name = doc["airport"] | "";
  shortenAirportName(api_name, name, sizeof(name));
  copyField(iata, sizeof(iata), api_iata);

  services::airport_cache::store(icao_code, iata, name);
  copyField(iata_out, iata_len, iata);
  copyField(name_out, name_len, name);
  return iata[0] != '\0' || name[0] != '\0';
}

bool airportNeedsDecode(const char* icao, const char* iata, const char* name) {
  return icao[0] != '\0' && iata[0] == '\0' && name[0] == '\0';
}

void fillMissingAirportNames(services::route_cache::RouteInfo* info) {
  if (info == nullptr) {
    return;
  }
  if (airportNeedsDecode(info->origin_icao, info->origin_iata, info->origin_name)) {
    resolveAirport(info->origin_icao, info->origin_iata, sizeof(info->origin_iata),
                   info->origin_name, sizeof(info->origin_name));
  }
  if (airportNeedsDecode(info->dest_icao, info->dest_iata, info->dest_name)) {
    resolveAirport(info->dest_icao, info->dest_iata, sizeof(info->dest_iata),
                   info->dest_name, sizeof(info->dest_name));
  }
}

void applyRouteToAircraft(Aircraft* ac, const services::route_cache::RouteInfo& info) {
  copyField(ac->origin, sizeof(ac->origin), info.origin_iata);
  copyField(ac->origin_icao, sizeof(ac->origin_icao), info.origin_icao);
  copyField(ac->origin_name, sizeof(ac->origin_name), info.origin_name);
  copyField(ac->dest, sizeof(ac->dest), info.dest_iata);
  copyField(ac->dest_icao, sizeof(ac->dest_icao), info.dest_icao);
  copyField(ac->dest_name, sizeof(ac->dest_name), info.dest_name);
  if (services::route_cache::routeHasDisplay(info)) {
    ac->route_fetched_ms = millis();
  }
}

void applyCachedRoute(Aircraft* ac) {
  services::route_cache::RouteInfo info;
  services::route_cache::clearRouteInfo(&info);
  if (!services::route_cache::lookup(ac->callsign, &info)) {
    return;
  }
  applyRouteToAircraft(ac, info);
}

bool parseRouteIcaos(const char* route, char* origin_icao, size_t origin_len,
                     char* dest_icao, size_t dest_len) {
  if (route == nullptr || route[0] == '\0' || origin_icao == nullptr ||
      dest_icao == nullptr || origin_len == 0 || dest_len == 0) {
    return false;
  }
  origin_icao[0] = '\0';
  dest_icao[0] = '\0';

  const char* first_dash = strchr(route, '-');
  const char* last_dash = strrchr(route, '-');
  if (first_dash == nullptr || last_dash == nullptr || last_dash[1] == '\0') {
    return false;
  }

  const size_t origin_n = static_cast<size_t>(first_dash - route);
  if (origin_n == 0 || origin_n >= origin_len) {
    return false;
  }
  memcpy(origin_icao, route, origin_n);
  origin_icao[origin_n] = '\0';
  copyField(dest_icao, dest_len, last_dash + 1);
  return origin_icao[0] != '\0' && dest_icao[0] != '\0';
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

bool fetchRoute(const char* callsign, services::route_cache::RouteInfo* out) {
  if (callsign == nullptr || callsign[0] == '\0' || out == nullptr) {
    return false;
  }
  services::route_cache::clearRouteInfo(out);

  services::route_cache::RouteInfo cached;
  services::route_cache::clearRouteInfo(&cached);
  if (services::route_cache::lookup(callsign, &cached)) {
    fillMissingAirportNames(&cached);
    if (airportNeedsDecode(cached.origin_icao, cached.origin_iata,
                           cached.origin_name) ||
        airportNeedsDecode(cached.dest_icao, cached.dest_iata, cached.dest_name)) {
      // Still missing names after retry — keep ICAOs cached.
      services::route_cache::store(callsign, cached);
    } else if (services::route_cache::routeHasDisplay(cached) ||
               cached.origin_icao[0] != '\0' || cached.dest_icao[0] != '\0') {
      services::route_cache::store(callsign, cached);
    }
    *out = cached;
    Serial.printf("route: cache hit %s  %s -> %s\n", callsign,
                  cached.origin_iata[0] ? cached.origin_iata
                                       : (cached.origin_icao[0] ? cached.origin_icao
                                                                : "?"),
                  cached.dest_iata[0] ? cached.dest_iata
                                     : (cached.dest_icao[0] ? cached.dest_icao : "?"));
    return services::route_cache::routeHasDisplay(cached);
  }

  String url = kRouteApiBase;
  url += callsign;
  Serial.printf("route: fetching %s\n", url.c_str());

  String payload;
  int code = 0;
  if (!httpsGetPayload(url, payload, &code)) {
    Serial.printf("route: HTTP %d for %s\n", code, callsign);
    if (code == HTTP_CODE_NOT_FOUND) {
      services::route_cache::RouteInfo empty;
      services::route_cache::clearRouteInfo(&empty);
      services::route_cache::store(callsign, empty);
    }
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("route: JSON error: %s\n", err.c_str());
    return false;
  }

  if (doc["status"].is<const char*>() || doc["error"].is<const char*>()) {
    services::route_cache::RouteInfo empty;
    services::route_cache::clearRouteInfo(&empty);
    services::route_cache::store(callsign, empty);
    return false;
  }

  const char* route = doc["route"] | "";
  services::route_cache::RouteInfo info;
  services::route_cache::clearRouteInfo(&info);
  if (!parseRouteIcaos(route, info.origin_icao, sizeof(info.origin_icao),
                       info.dest_icao, sizeof(info.dest_icao))) {
    services::route_cache::store(callsign, info);
    return false;
  }

  resolveAirport(info.origin_icao, info.origin_iata, sizeof(info.origin_iata),
                 info.origin_name, sizeof(info.origin_name));
  resolveAirport(info.dest_icao, info.dest_iata, sizeof(info.dest_iata),
                 info.dest_name, sizeof(info.dest_name));

  Serial.printf("route: %s  %s (%s) -> %s (%s)\n", callsign,
                info.origin_iata[0] ? info.origin_iata : info.origin_icao,
                info.origin_name[0] ? info.origin_name : "-",
                info.dest_iata[0] ? info.dest_iata : info.dest_icao,
                info.dest_name[0] ? info.dest_name : "-");

  services::route_cache::store(callsign, info);
  *out = info;
  return services::route_cache::routeHasDisplay(info);
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  String payload;
  int code = 0;
  if (!httpsGetPayload(url, payload, &code)) {
    Serial.printf("adsb: HTTP %d\n", code);
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    s_aircraft_count = 0;
    return true;
  }

  size_t n = 0;
  for (JsonObject plane : ac) {
    if (n >= kMaxAircraft) {
      break;
    }
    if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
      continue;
    }
    if (isOnGround(plane) && !config::kAdsbShowGroundAircraft) {
      continue;
    }

    s_aircraft[n].lat = plane["lat"].as<float>();
    s_aircraft[n].lon = plane["lon"].as<float>();
    s_aircraft[n].nose_deg = pickNoseHeading(plane);
    s_aircraft[n].track_deg = pickTrackHeading(plane);
    s_aircraft[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&s_aircraft[n], plane);
    applyCachedRoute(&s_aircraft[n]);

    ++n;
  }

  s_aircraft_count = n;
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
