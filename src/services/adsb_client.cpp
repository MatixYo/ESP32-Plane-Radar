#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>

#include "config.h"
#include "services/aircraft_motion.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

using motion::AircraftTrack;

AircraftTrack s_tracks[kMaxAircraft];
AircraftTrack s_next_tracks[kMaxAircraft];
Aircraft s_incoming[kMaxAircraft];
Aircraft s_display[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;
portMUX_TYPE s_tracks_lock = portMUX_INITIALIZER_UNLOCKED;

int findTrackById(const char* id) {
  if (id == nullptr || id[0] == '\0') {
    return -1;
  }
  for (size_t i = 0; i < s_aircraft_count; ++i) {
    if (strcmp(s_tracks[i].aircraft.id, id) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void replaceTracks(const Aircraft* incoming, size_t count,
                   unsigned long now_ms) {
  taskENTER_CRITICAL(&s_tracks_lock);
  for (size_t i = 0; i < count; ++i) {
    const int previous = motion::hasStableId(incoming[i])
                             ? findTrackById(incoming[i].id)
                             : -1;
    s_next_tracks[i] = previous >= 0
                           ? motion::updateTrack(s_tracks[previous], incoming[i],
                                                 now_ms)
                           : motion::makeInitialTrack(incoming[i], now_ms);
  }
  for (size_t i = 0; i < count; ++i) {
    s_tracks[i] = s_next_tracks[i];
  }
  s_aircraft_count = count;
  taskEXIT_CRITICAL(&s_tracks_lock);
}

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int performGetWithPoll(HTTPClient& http) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollNetwork();
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  (void)content_length;
  payload.reserve(4096);

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

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "hex", ac->id, sizeof(ac->id));
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    strncpy(ac->callsign, ac->id, sizeof(ac->callsign) - 1);
    ac->callsign[sizeof(ac->callsign) - 1] = '\0';
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() {
  taskENTER_CRITICAL(&s_tracks_lock);
  const size_t count = s_aircraft_count;
  taskEXIT_CRITICAL(&s_tracks_lock);
  return count;
}

const Aircraft* aircraftList(unsigned long now_ms, size_t* count) {
  taskENTER_CRITICAL(&s_tracks_lock);
  const size_t display_count = s_aircraft_count;
  for (size_t i = 0; i < display_count; ++i) {
    s_display[i] = s_tracks[i].aircraft;
    const motion::Position position =
        motion::displayPosition(s_tracks[i], now_ms);
    s_display[i].lat = position.lat;
    s_display[i].lon = position.lon;
  }
  taskEXIT_CRITICAL(&s_tracks_lock);
  if (count != nullptr) {
    *count = display_count;
  }
  return s_display;
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const float dist_nm = kmToNauticalMiles(fetch_radius_km);

  String url = kApiBase;
  url += String(center_lat, 6);
  url += "/lon/";
  url += String(center_lon, 6);
  url += "/dist/";
  url += String(dist_nm, 1);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("adsb: http.begin failed");
    return false;
  }

  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload;
  if (!readResponseBodyWithPoll(http, payload)) {
    Serial.println("adsb: empty response");
    http.end();
    return false;
  }
  http.end();

  const size_t payload_len = payload.length();
  size_t doc_capacity = payload_len * 2u;
  if (doc_capacity < 24 * 1024) {
    doc_capacity = 24 * 1024;
  }
  if (doc_capacity > 96 * 1024) {
    doc_capacity = 96 * 1024;
  }

  DynamicJsonDocument doc(doc_capacity);
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("adsb: JSON parse error: %s (payload=%u bytes)\n",
                  err.c_str(), static_cast<unsigned>(payload_len));
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    replaceTracks(nullptr, 0, millis());
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

    s_incoming[n].lat = plane["lat"].as<float>();
    s_incoming[n].lon = plane["lon"].as<float>();
    s_incoming[n].nose_deg = pickNoseHeading(plane);
    s_incoming[n].track_deg = pickTrackHeading(plane);
    s_incoming[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&s_incoming[n], plane);
    ++n;
  }

  replaceTracks(s_incoming, n, millis());
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
