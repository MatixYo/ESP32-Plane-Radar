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
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;

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

/**
 * Stream wrapper that keeps the rest of the firmware alive during blocking
 * HTTP reads and bounds the whole body transfer with a single deadline.
 *
 * The response is parsed straight off this stream rather than buffered: a
 * busy sector easily exceeds 20 kB, and the heap is too fragmented to hold
 * that in one contiguous allocation.
 */
class PollingStream : public Stream {
 public:
  PollingStream(WiFiClient* source, unsigned long deadline)
      : source_(source), deadline_(deadline) {}

  int available() override { return source_->available(); }
  int peek() override { return waitForData() ? source_->peek() : -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }

  int read() override { return waitForData() ? source_->read() : -1; }

  size_t readBytes(char* buffer, size_t length) override {
    size_t got = 0;
    while (got < length && waitForData()) {
      const int n = source_->read(reinterpret_cast<uint8_t*>(buffer + got),
                                  length - got);
      if (n > 0) {
        got += static_cast<size_t>(n);
      }
    }
    return got;
  }

 private:
  /** Blocks until a byte is ready, the peer hangs up, or the deadline hits. */
  bool waitForData() {
    while (source_->available() <= 0) {
      if (millis() >= deadline_) {
        return false;
      }
      if (!source_->connected()) {
        return source_->available() > 0;
      }
      pollNetwork();
      delay(1);
    }
    return true;
  }

  WiFiClient* source_;
  unsigned long deadline_;
};

/** Keeps only the fields the radar actually renders. */
void buildPlaneFilter(JsonDocument& filter) {
  static const char* const kKeys[] = {
      "lat", "lon", "true_heading", "mag_heading", "track",    "dir", "gs",
      "tas", "ias", "alt_baro",     "alt_geom",    "flight",   "hex", "t"};
  for (const char* key : kKeys) {
    filter[key] = true;
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

  WiFiClient* source = http.getStreamPtr();
  if (source == nullptr) {
    Serial.println("adsb: no response stream");
    http.end();
    return false;
  }

  PollingStream stream(source, millis() + kRequestTimeoutMs);
  stream.setTimeout(kRequestTimeoutMs);

  // Walk to the start of the "ac" array; two steps so `"ac" : [` also matches.
  if (!stream.find("\"ac\"") || !stream.find("[")) {
    Serial.println("adsb: no aircraft array in response");
    http.end();
    s_aircraft_count = 0;
    return true;
  }

  JsonDocument filter;
  buildPlaneFilter(filter);

  size_t n = 0;
  bool ok = true;
  if (stream.peek() != ']') {  // guard against an empty "ac":[] array
    do {
      // One aircraft at a time: peak memory stays a few hundred bytes
      // instead of the ~20 kB the full document would need.
      JsonDocument plane_doc;
      const DeserializationError err = deserializeJson(
          plane_doc, stream, DeserializationOption::Filter(filter));
      if (err) {
        Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
        ok = false;
        break;
      }

      JsonObject plane = plane_doc.as<JsonObject>();
      if (n < kMaxAircraft && plane["lat"].is<float>() &&
          plane["lon"].is<float>() &&
          (config::kAdsbShowGroundAircraft || !isOnGround(plane))) {
        s_aircraft[n].lat = plane["lat"].as<float>();
        s_aircraft[n].lon = plane["lon"].as<float>();
        s_aircraft[n].nose_deg = pickNoseHeading(plane);
        s_aircraft[n].track_deg = pickTrackHeading(plane);
        s_aircraft[n].gs_knots = pickGroundSpeed(plane);
        fillTagFields(&s_aircraft[n], plane);
        ++n;
      }

      if (n >= kMaxAircraft) {
        break;  // enough to draw; drop the rest of the body
      }
    } while (stream.findUntil(",", "]"));
  }

  http.end();

  if (!ok && n == 0) {
    return false;
  }

  s_aircraft_count = n;
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
