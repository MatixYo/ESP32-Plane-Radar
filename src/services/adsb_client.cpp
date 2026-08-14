#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>

#include "config.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[2][kMaxAircraft];
size_t s_aircraft_count[2] = {0, 0};
volatile uint8_t s_active_aircraft = 0;
PollFn s_poll_fn = nullptr;

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

class PollingStreamReader {
 public:
  explicit PollingStreamReader(Stream& stream) : stream_(stream) {}

  int read() {
    char value = 0;
    const bool received = stream_.readBytes(&value, 1) == 1;
    pollOccasionally(1);
    return received ? static_cast<unsigned char>(value) : -1;
  }

  size_t readBytes(char* buffer, size_t length) {
    // Keep reads short so display animation is serviced while JSON arrives.
    constexpr size_t kChunkSize = 256;
    const size_t chunk = std::min(length, kChunkSize);
    const size_t count = stream_.readBytes(buffer, chunk);
    pollNetwork();
    bytes_until_poll_ = kChunkSize;
    return count;
  }

 private:
  void pollOccasionally(size_t count) {
    if (count >= bytes_until_poll_) {
      pollNetwork();
      bytes_until_poll_ = 256;
    } else {
      bytes_until_poll_ -= count;
    }
  }

  Stream& stream_;
  size_t bytes_until_poll_ = 256;
};

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

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

void buildAircraftJsonFilter(JsonDocument& filter) {
  JsonObject plane = filter["ac"][0].to<JsonObject>();
  plane["lat"] = true;
  plane["lon"] = true;
  plane["true_heading"] = true;
  plane["mag_heading"] = true;
  plane["track"] = true;
  plane["dir"] = true;
  plane["gs"] = true;
  plane["tas"] = true;
  plane["ias"] = true;
  plane["alt_baro"] = true;
  plane["alt_geom"] = true;
  plane["flight"] = true;
  plane["hex"] = true;
  plane["t"] = true;
}

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
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count[s_active_aircraft]; }

const Aircraft* aircraftList() { return s_aircraft[s_active_aircraft]; }

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const uint8_t update_index = static_cast<uint8_t>(1U - s_active_aircraft);
  Aircraft* updated = s_aircraft[update_index];
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

  // Parse directly from the network stream so large search radii do not need
  // one contiguous response buffer. The filter retains only displayed fields.
  JsonDocument filter;
  buildAircraftJsonFilter(filter);
  JsonDocument doc;
  PollingStreamReader reader(http.getStream());
  const DeserializationError err =
      deserializeJson(doc, reader, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray ac = doc["ac"].as<JsonArray>();
  if (ac.isNull()) {
    s_aircraft_count[update_index] = 0;
    s_active_aircraft = update_index;
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

    updated[n].lat = plane["lat"].as<float>();
    updated[n].lon = plane["lon"].as<float>();
    updated[n].nose_deg = pickNoseHeading(plane);
    updated[n].track_deg = pickTrackHeading(plane);
    updated[n].gs_knots = pickGroundSpeed(plane);
    fillTagFields(&updated[n], plane);
    ++n;
  }

  s_aircraft_count[update_index] = n;
  s_active_aircraft = update_index;
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
