#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#include "config.h"
#include "services/http_body_framing.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
unsigned long s_last_update_ms = 0;
PollFn s_poll_fn = nullptr;
SemaphoreHandle_t s_mutex = nullptr;

/** Publish parsed aircraft to the shared buffer atomically. */
void publish(const Aircraft* src, size_t count) {
  if (s_mutex != nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  }
  for (size_t i = 0; i < count; ++i) {
    s_aircraft[i] = src[i];
  }
  s_aircraft_count = count;
  s_last_update_ms = millis();  // base time for dead-reckoning
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
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

/**
 * Pumps the socket one byte at a time, in blocks, without buffering the whole
 * response.
 *
 * Each refill runs the network poll callback, which is why HTTPClient's own
 * body readers (getString/writeToStream) can't be used here -- they block
 * without giving the fetch task a chance to poll. That also means they can't
 * de-chunk for us, so the wire image comes out raw and BodyFramer unwraps it.
 *
 * Where the body ends is BodyFramer's business, not this class's: it stops
 * pulling at the right byte. Reading a little past that point into buffer_ is
 * harmless while the connection is torn down after every fetch.
 */
class PollingSocketSource {
 public:
  PollingSocketSource(HTTPClient& http, WiFiClient& stream,
                      unsigned long deadline)
      : http_(&http), stream_(&stream), deadline_(deadline) {}

  /** Next raw byte, or -1 once the socket closes or the deadline passes. */
  int read() {
    if (pos_ >= len_ && !refill()) {
      return -1;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

 private:
  bool refill() {
    pos_ = 0;
    len_ = 0;
    while (millis() < deadline_) {
      pollNetwork();
      const int available = stream_->available();
      if (available > 0) {
        const int to_read = available > static_cast<int>(sizeof(buffer_))
                                ? static_cast<int>(sizeof(buffer_))
                                : available;
        const int read_bytes = stream_->readBytes(buffer_, to_read);
        if (read_bytes > 0) {
          len_ = static_cast<size_t>(read_bytes);
          return true;
        }
      }
      if (!http_->connected() && stream_->available() <= 0) {
        break;  // server closed and the socket is drained
      }
      delay(1);
    }
    return false;
  }

  HTTPClient* http_;
  WiFiClient* stream_;
  unsigned long deadline_;
  char buffer_[512];
  size_t pos_ = 0;
  size_t len_ = 0;
};

using BodyReader = services::http::BodyFramer<PollingSocketSource>;

/**
 * Builds the deserialization filter. The keys below are the only ones the
 * radar reads, out of the ~40 each adsb.fi v3 record carries; the parser
 * skips the rest (rssi, mlat, tisb, nic, messages, ...) without storing them.
 */
void buildAircraftFilter(JsonDocument& filter) {
  // A filter array applies its first element to every element of the input.
  JsonObject plane = filter["ac"].add<JsonObject>();
  plane["lat"] = true;
  plane["lon"] = true;
  plane["track"] = true;
  plane["true_heading"] = true;
  plane["mag_heading"] = true;
  plane["dir"] = true;
  plane["gs"] = true;
  plane["tas"] = true;
  plane["ias"] = true;
  plane["alt_baro"] = true;
  plane["alt_geom"] = true;
  plane["seen_pos"] = true;
  plane["flight"] = true;
  plane["hex"] = true;
  plane["t"] = true;
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

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

unsigned long lastUpdateMs() { return s_last_update_ms; }

size_t snapshotAircraft(Aircraft* out, size_t max_out,
                        unsigned long* out_last_update_ms) {
  if (s_mutex != nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  }
  const size_t count =
      s_aircraft_count < max_out ? s_aircraft_count : max_out;
  for (size_t i = 0; i < count; ++i) {
    out[i] = s_aircraft[i];
  }
  if (out_last_update_ms != nullptr) {
    *out_last_update_ms = s_last_update_ms;
  }
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
  return count;
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

  // HTTPClient only records Transfer-Encoding in the collected headers when
  // it is asked for up front; _transferEncoding itself is private.
  static const char* kWantedHeaders[] = {"Transfer-Encoding"};
  http.collectHeaders(kWantedHeaders, 1);

  http.setTimeout(kRequestTimeoutMs);
  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("adsb: HTTP %d\n", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    Serial.println("adsb: no response stream");
    http.end();
    return false;
  }

  // Parse straight off the socket, with a filter that keeps only the fields
  // the radar reads. The body is never held in RAM as a whole and the skipped
  // fields never get a document slot, so peak heap stays flat no matter how
  // many aircraft the API returns.
  JsonDocument filter;
  buildAircraftFilter(filter);

  // On HTTP/1.1 the CDN answers with Transfer-Encoding: chunked, and
  // getStreamPtr() hands back the raw socket -- chunk sizes and all. BodyFramer
  // strips that framing back off.
  const services::http::BodyFraming framing =
      http.header("Transfer-Encoding").equalsIgnoreCase("chunked")
          ? services::http::BodyFraming::kChunked
          : services::http::BodyFraming::kIdentity;

  PollingSocketSource source(http, *stream, millis() + kRequestTimeoutMs);
  BodyReader body(source, framing, http.getSize());
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  // Read off the terminating chunk the parser stopped short of, so the socket
  // sits at the end of the message. Not needed while every fetch builds its own
  // connection, but a prerequisite for ever reusing one.
  body.drain();
  http.end();
  if (err) {
    if (body.framingError()) {
      Serial.println("adsb: malformed chunked body");
    } else if (body.bytesRead() == 0) {
      Serial.println("adsb: empty response");
    } else {
      Serial.printf("adsb: JSON parse error: %s\n", err.c_str());
    }
    return false;
  }

  // Parse into a local buffer, then publish atomically so a reader on another
  // thread never sees a half-updated list.
  Aircraft parsed[kMaxAircraft];
  size_t n = 0;
  JsonArray ac = doc["ac"].as<JsonArray>();
  if (!ac.isNull()) {
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

      parsed[n].lat = plane["lat"].as<float>();
      parsed[n].lon = plane["lon"].as<float>();
      parsed[n].nose_deg = pickNoseHeading(plane);
      parsed[n].track_deg = pickTrackHeading(plane);
      parsed[n].gs_knots = pickGroundSpeed(plane);

      // seen_pos: seconds since this position was measured. Use it as the
      // dead-reckoning age offset, capped so a very stale fix isn't flung far.
      float seen_pos = 0.0f;
      readJsonFloat(plane, "seen_pos", &seen_pos);
      if (seen_pos < 0.0f) seen_pos = 0.0f;
      if (seen_pos > 30.0f) seen_pos = 30.0f;
      parsed[n].pos_age_ms = static_cast<uint32_t>(seen_pos * 1000.0f);

      fillTagFields(&parsed[n], plane);
      ++n;
    }
  }

  publish(parsed, n);
  Serial.printf("adsb: %u aircraft\n", static_cast<unsigned>(n));
  return true;
}

}  // namespace services::adsb
