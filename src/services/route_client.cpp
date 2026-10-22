#include "services/route_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cctype>
#include <cmath>
#include <cstring>

#include "services/adsb_client.h"
#include "services/ascii_fold.h"

namespace services::route {

namespace {

constexpr char kApiBase[] = "https://api.adsbdb.com/v0/callsign/";
constexpr int kConnectTimeoutMs = 5000;
constexpr int kRequestTimeoutMs = 6000;
constexpr size_t kMaxBodyBytes = 8192;

constexpr size_t kCacheSize = 48;

constexpr unsigned long kFoundTtlMs = 2UL * 60UL * 60UL * 1000UL;
constexpr unsigned long kNotFoundTtlMs = 30UL * 60UL * 1000UL;
constexpr unsigned long kErrorTtlMs = 30UL * 1000UL;
constexpr unsigned long kRateLimitBackoffMs = 60UL * 1000UL;

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 0.01745329252f;

enum class State : uint8_t { kEmpty, kFound, kNotFound, kError };
enum class FetchResult : uint8_t { kOk, kNotFound, kError, kRateLimited };

struct Entry {
  char callsign[9];
  Airport origin;
  Airport destination;
  State state;
  unsigned long fetched_ms;
  unsigned long last_used_ms;
};

Entry s_cache[kCacheSize];
SemaphoreHandle_t s_mutex = nullptr;
unsigned long s_backoff_until_ms = 0;
bool s_backoff_active = false;

void lock() {
  if (s_mutex != nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  }
}

void unlock() {
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
}

unsigned long ttlFor(State state) {
  switch (state) {
    case State::kFound:
      return kFoundTtlMs;
    case State::kNotFound:
      return kNotFoundTtlMs;
    default:
      return kErrorTtlMs;
  }
}

Entry* findLocked(const char* callsign) {
  for (Entry& e : s_cache) {
    if (e.state != State::kEmpty && strcmp(e.callsign, callsign) == 0) {
      return &e;
    }
  }
  return nullptr;
}

Entry* allocLocked() {
  Entry* victim = &s_cache[0];
  for (Entry& e : s_cache) {
    if (e.state == State::kEmpty) {
      return &e;
    }
    if (static_cast<long>(victim->last_used_ms - e.last_used_ms) > 0) {
      victim = &e;
    }
  }
  return victim;
}

void store(const char* callsign, State state, const Airport* origin,
           const Airport* destination) {
  const unsigned long now = millis();
  lock();
  Entry* e = findLocked(callsign);
  if (e == nullptr) {
    e = allocLocked();
    strncpy(e->callsign, callsign, sizeof(e->callsign) - 1);
    e->callsign[sizeof(e->callsign) - 1] = '\0';
  }
  e->state = state;
  e->fetched_ms = now;
  e->last_used_ms = now;
  memset(&e->origin, 0, sizeof(e->origin));
  memset(&e->destination, 0, sizeof(e->destination));
  if (origin != nullptr) {
    e->origin = *origin;
  }
  if (destination != nullptr) {
    e->destination = *destination;
  }
  unlock();
}

bool needsFetch(const char* callsign) {
  const unsigned long now = millis();
  lock();
  const Entry* e = findLocked(callsign);
  const bool due = (e == nullptr) || (now - e->fetched_ms >= ttlFor(e->state));
  unlock();
  return due;
}

bool validCallsign(const char* cs) {
  const size_t n = strlen(cs);
  if (n < 2 || n > 8) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (!isalnum(static_cast<unsigned char>(cs[i]))) {
      return false;
    }
  }
  return true;
}

void copyAirport(JsonObject airport, Airport* out) {
  memset(out, 0, sizeof(*out));
  if (airport.isNull()) {
    return;
  }
  for (const char* key : {"iata_code", "icao_code"}) {
    if (airport[key].is<const char*>()) {
      const char* code = airport[key].as<const char*>();
      if (code != nullptr && code[0] != '\0') {
        strncpy(out->code, code, sizeof(out->code) - 1);
        break;
      }
    }
  }
  if (airport["municipality"].is<const char*>()) {
    services::foldToAscii(airport["municipality"].as<const char*>(), out->city,
                          sizeof(out->city));
  }
}

const char* airportLabel(const Airport& a) {
  if (a.city[0] != '\0') {
    return a.city;
  }
  return a.code[0] != '\0' ? a.code : "?";
}

FetchResult fetchRoute(const char* callsign, Airport* origin,
                       Airport* destination) {
  String url = kApiBase;
  url += callsign;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kRequestTimeoutMs);
  http.useHTTP10(true);
  if (!http.begin(client, url)) {
    Serial.println("route: http.begin failed");
    return FetchResult::kError;
  }

  const int code = http.GET();
  if (code == 404) {
    http.end();
    return FetchResult::kNotFound;
  }
  if (code == 429) {
    http.end();
    return FetchResult::kRateLimited;
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("route: HTTP %d for %s\n", code, callsign);
    http.end();
    return FetchResult::kError;
  }
  if (http.getSize() > static_cast<int>(kMaxBodyBytes)) {
    http.end();
    return FetchResult::kError;
  }

  const String body = http.getString();
  http.end();

  JsonDocument filter;
  for (const char* side : {"origin", "destination"}) {
    filter["response"]["flightroute"][side]["iata_code"] = true;
    filter["response"]["flightroute"][side]["icao_code"] = true;
    filter["response"]["flightroute"][side]["municipality"] = true;
  }

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("route: JSON parse error: %s\n", err.c_str());
    return FetchResult::kError;
  }

  JsonObject route = doc["response"]["flightroute"].as<JsonObject>();
  if (route.isNull()) {
    return FetchResult::kNotFound;
  }

  copyAirport(route["origin"].as<JsonObject>(), origin);
  copyAirport(route["destination"].as<JsonObject>(), destination);
  return destination->code[0] != '\0' ? FetchResult::kOk
                                       : FetchResult::kNotFound;
}

}  // namespace

void init() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

bool lookup(const char* callsign, Route* out) {
  if (callsign == nullptr || callsign[0] == '\0') {
    return false;
  }
  bool found = false;
  lock();
  Entry* e = findLocked(callsign);
  if (e != nullptr && e->state == State::kFound) {
    if (out != nullptr) {
      out->origin = e->origin;
      out->destination = e->destination;
    }
    e->last_used_ms = millis();
    found = true;
  }
  unlock();
  return found;
}

size_t fetchPending(double center_lat, double center_lon, float max_dist_km,
                    size_t max_lookups) {
  if (max_lookups == 0) {
    return 0;
  }
  if (s_backoff_active &&
      static_cast<long>(millis() - s_backoff_until_ms) < 0) {
    return 0;
  }
  s_backoff_active = false;

  static services::adsb::Aircraft planes[services::adsb::kMaxAircraft];
  static float dist_km[services::adsb::kMaxAircraft];
  bool tried[services::adsb::kMaxAircraft] = {};

  unsigned long base_ms = 0;
  const size_t n = services::adsb::snapshotAircraft(
      planes, services::adsb::kMaxAircraft, &base_ms);

  const float cos_lat =
      cosf(static_cast<float>(center_lat) * kDegToRad);
  for (size_t i = 0; i < n; ++i) {
    const float dx =
        static_cast<float>(planes[i].lon - center_lon) * kKmPerDeg * cos_lat;
    const float dy = static_cast<float>(planes[i].lat - center_lat) * kKmPerDeg;
    dist_km[i] = sqrtf(dx * dx + dy * dy);
  }

  size_t requests = 0;
  while (requests < max_lookups) {
    int best = -1;
    for (size_t i = 0; i < n; ++i) {
      if (tried[i] || !planes[i].has_flight_callsign ||
          dist_km[i] > max_dist_km || !validCallsign(planes[i].callsign) ||
          !needsFetch(planes[i].callsign)) {
        continue;
      }
      if (best < 0 || dist_km[i] < dist_km[best]) {
        best = static_cast<int>(i);
      }
    }
    if (best < 0) {
      break;
    }
    tried[best] = true;

    Airport origin = {};
    Airport destination = {};
    const FetchResult result =
        fetchRoute(planes[best].callsign, &origin, &destination);
    ++requests;

    switch (result) {
      case FetchResult::kOk:
        store(planes[best].callsign, State::kFound, &origin, &destination);
        Serial.printf(
            "route: %s %s to %s (%s>%s) [heap free %u, lowest %u]\n",
            planes[best].callsign, airportLabel(origin),
            airportLabel(destination), origin.code[0] ? origin.code : "?",
            destination.code, static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMinFreeHeap()));
        break;
      case FetchResult::kNotFound:
        store(planes[best].callsign, State::kNotFound, nullptr, nullptr);
        Serial.printf("route: %s no route found\n", planes[best].callsign);
        break;
      case FetchResult::kRateLimited:
        Serial.println("route: rate limited, backing off");
        s_backoff_until_ms = millis() + kRateLimitBackoffMs;
        s_backoff_active = true;
        return requests;
      case FetchResult::kError:
        store(planes[best].callsign, State::kError, nullptr, nullptr);
        break;
    }
  }
  return requests;
}

}  // namespace services::route
