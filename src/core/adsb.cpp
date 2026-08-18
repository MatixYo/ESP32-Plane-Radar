#include "core/adsb.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"

namespace core::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
constexpr unsigned long kRequestTimeoutMs = 10000;

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
platform::PollFn s_poll_fn = nullptr;

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

/** Where the airframe points: true heading, else magnetic, else track. */
float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) return v;
  if (readJsonFloat(plane, "mag_heading", &v)) return v;
  if (readJsonFloat(plane, "track", &v)) return v;
  if (readJsonFloat(plane, "dir", &v)) return v;
  return 0.0f;
}

/** Where it is actually going: track first, then any heading. */
float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) return v;
  if (readJsonFloat(plane, "true_heading", &v)) return v;
  if (readJsonFloat(plane, "mag_heading", &v)) return v;
  if (readJsonFloat(plane, "dir", &v)) return v;
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) return v;
  if (readJsonFloat(plane, "tas", &v)) return v;
  if (readJsonFloat(plane, "ias", &v)) return v;
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

/**
 * Byte cursor over the response body, with the single character of pushback
 * that walking the array needs: the loop reads a delimiter to decide whether
 * another element follows, and has to hand the first character of that element
 * back before ArduinoJson sees it. read() and readBytes() are the two methods
 * the deserializer calls.
 */
class ElementScanner {
 public:
  explicit ElementScanner(platform::BodyReader& src) : src_(src) {}

  int read() {
    if (pending_ >= 0) {
      const int c = pending_;
      pending_ = -1;
      return c;
    }
    const int c = src_.read();
    if (c < 0) {
      exhausted_ = true;
    }
    return c;
  }

  size_t readBytes(char* buf, size_t len) {
    size_t taken = 0;
    if (pending_ >= 0 && len > 0) {
      buf[0] = static_cast<char>(pending_);
      pending_ = -1;
      taken = 1;
    }
    const size_t got = taken + src_.readBytes(buf + taken, len - taken);
    if (got < len) {
      exhausted_ = true;
    }
    return got;
  }

  void unread(int c) { pending_ = c; }

  /** True once the body ran out, which separates a short reply from a quiet sky. */
  bool exhausted() const { return exhausted_; }

  int nextNonSpace() {
    int c;
    do {
      c = read();
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    return c;
  }

  /**
   * Leaves the cursor just past the '[' that opens the aircraft array.
   *
   * It matches the key rather than the first '[' in the body because the reply
   * also carries "msg", "now" and "total", and because a sky with no traffic
   * answers "ac":null — which lands on the ':' branch and reports no array.
   */
  bool seekAircraftArray() {
    static constexpr char kKey[] = "\"ac\"";
    constexpr size_t kKeyLen = sizeof(kKey) - 1;

    size_t matched = 0;
    int c;
    while (matched < kKeyLen && (c = read()) >= 0) {
      if (static_cast<char>(c) == kKey[matched]) {
        ++matched;
      } else {
        matched = static_cast<char>(c) == kKey[0] ? 1 : 0;
      }
    }
    if (matched < kKeyLen) {
      return false;
    }
    return nextNonSpace() == ':' && nextNonSpace() == '[';
  }

 private:
  platform::BodyReader& src_;
  int pending_ = -1;
  bool exhausted_ = false;
};

/**
 * Decodes the aircraft array element by element.
 *
 * Nothing here scales with the size of the reply: one aircraft is held as a
 * JSON document at a time, and the decoded fields go straight into the fixed
 * s_aircraft slots. The whole body used to be buffered first, which threw
 * std::bad_alloc — and so aborted the firmware — as soon as a wider range
 * pushed the reply past what the heap could hand out in one block.
 */
bool parseBody(platform::BodyReader& body) {
  ElementScanner in(body);

  if (!in.seekAircraftArray()) {
    s_aircraft_count = 0;
    return !in.exhausted();
  }

  size_t n = 0;
  bool ok = true;
  int c = in.nextNonSpace();
  while (c != ']') {
    if (c < 0) {
      ok = false;
      break;
    }
    in.unread(c);

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, in);
    if (err) {
      platform::logf("adsb: JSON parse error: %s\n", err.c_str());
      ok = false;
      break;
    }

    JsonObject plane = doc.as<JsonObject>();
    if (!plane.isNull() && plane["lat"].is<float>() &&
        plane["lon"].is<float>() &&
        (config::kAdsbShowGroundAircraft || !isOnGround(plane))) {
      s_aircraft[n].lat = plane["lat"].as<float>();
      s_aircraft[n].lon = plane["lon"].as<float>();
      s_aircraft[n].nose_deg = pickNoseHeading(plane);
      s_aircraft[n].track_deg = pickTrackHeading(plane);
      s_aircraft[n].gs_knots = pickGroundSpeed(plane);
      fillTagFields(&s_aircraft[n], plane);
      ++n;
      if (n >= kMaxAircraft) {
        // Whatever is left goes unread; the transport closes the connection.
        break;
      }
    }

    c = in.nextNonSpace();
    if (c == ',') {
      c = in.nextNonSpace();
    } else if (c != ']') {
      ok = false;
      break;
    }
  }

  s_aircraft_count = n;
  return ok;
}

}  // namespace

void setPollFn(platform::PollFn fn) { s_poll_fn = fn; }

void clear() { s_aircraft_count = 0; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

void buildUrl(char* buf, size_t len, double center_lat, double center_lon,
              float fetch_radius_km) {
  snprintf(buf, len, "%s%.6f/lon/%.6f/dist/%.1f", kApiBase, center_lat,
           center_lon, static_cast<double>(kmToNauticalMiles(fetch_radius_km)));
}

bool parseResponse(const char* json) {
  platform::MemoryBodyReader body(json, strlen(json));
  return parseBody(body);
}

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  char url[160];
  buildUrl(url, sizeof(url), center_lat, center_lon, fetch_radius_km);

  if (!platform::HttpClient::get(url, parseBody, kRequestTimeoutMs,
                                 s_poll_fn)) {
    return false;
  }

  platform::logf("adsb: %u aircraft\n",
                 static_cast<unsigned>(s_aircraft_count));
  return true;
}

}  // namespace core::adsb
