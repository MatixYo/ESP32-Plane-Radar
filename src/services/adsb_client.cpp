#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

#include "config.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectTimeoutMs = 5000;  // TLS handshake needs room
constexpr int kConnectAttempts = 1;  // a stalled TLS connect blocks the UI; retry next poll instead
constexpr unsigned long kRequestTimeoutMs = 6000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
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

bool httpGetJson(const String& url, const char* tag, JsonDocument& doc,
                 const JsonDocument& filter) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.printf("%s: http.begin failed\n", tag);
    return false;
  }

  http.useHTTP10(true);
  http.setTimeout(kRequestTimeoutMs);
  http.setConnectTimeout(kConnectTimeoutMs);

  int code = 0;
  for (int attempt = 0; attempt < kConnectAttempts; ++attempt) {
    pollNetwork();
    code = http.GET();
    if (code > 0) {
      break;
    }
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("%s: HTTP %d\n", tag, code);
    http.end();
    return false;
  }

  const DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("%s: JSON parse error: %s\n", tag, err.c_str());
    return false;
  }
  return true;
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  copyJsonStringTrimmed(plane, "hex", ac->hex, sizeof(ac->hex));
  copyJsonStringTrimmed(plane, "r", ac->reg, sizeof(ac->reg));
  copyJsonStringTrimmed(plane, "desc", ac->desc, sizeof(ac->desc));
  copyJsonStringTrimmed(plane, "squawk", ac->squawk, sizeof(ac->squawk));

  ac->on_ground = isOnGround(plane);
  ac->baro_rate_fpm = 0.0f;
  if (!readJsonFloat(plane, "baro_rate", &ac->baro_rate_fpm)) {
    readJsonFloat(plane, "geom_rate", &ac->baro_rate_fpm);
  }

  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  // Keep only the fields we render; the rest never reaches RAM.
  JsonDocument filter;
  JsonObject f = filter["ac"].add<JsonObject>();
  for (const char* key :
       {"lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
        "tas", "ias", "alt_baro", "alt_geom", "flight", "hex", "t",
        "category"}) {
    f[key] = true;
  }

  JsonDocument doc;
  if (!httpGetJson(url, "adsb", doc, filter)) {
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    s_aircraft_count = 0;
    return true;
  }

  constexpr float kKmPerDeg = 111.0f;
  constexpr float kDegToRad = 3.14159265f / 180.0f;
  const float center_lat_rad = static_cast<float>(center_lat) * kDegToRad;

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

    // Calculate distance and bearing from radar center
    const float dx = static_cast<float>(s_aircraft[n].lon - center_lon) *
                     kKmPerDeg * cosf(center_lat_rad);
    const float dy = static_cast<float>(s_aircraft[n].lat - center_lat) * kKmPerDeg;
    s_aircraft[n].dist_km = sqrtf(dx * dx + dy * dy);
    float brg = atan2f(dx, dy) * 57.2957795f;
    if (brg < 0.0f) {
      brg += 360.0f;
    }
    s_aircraft[n].bearing_deg = brg;

    ++n;
  }

  // Sort aircraft closest first
  for (size_t i = 1; i < n; ++i) {
    const Aircraft key = s_aircraft[i];
    size_t j = i;
    while (j > 0 && s_aircraft[j - 1].dist_km > key.dist_km) {
      s_aircraft[j] = s_aircraft[j - 1];
      --j;
    }
    s_aircraft[j] = key;
  }

  s_aircraft_count = n;
  Serial.printf("adsb: %u aircraft (sorted closest first)\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
