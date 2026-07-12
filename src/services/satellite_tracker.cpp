#include "services/satellite_tracker.h"

#include <HTTPClient.h>
#include <Sgp4.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <cstring>

#include "config.h"

namespace services::satellites {

namespace {

constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 15000;
constexpr time_t kMinSaneUnixTime = 1700000000;

struct SatelliteTle {
  char name[25];
  char line1[72];
  char line2[72];
};

struct Candidate {
  char name[25];
  float az;
  float el;
  float dist;
};

SatelliteTle s_catalog[kMaxCatalog];
size_t s_catalog_count = 0;

Satellite s_visible[kMaxVisible];
size_t s_visible_count = 0;
PollFn s_poll_fn = nullptr;
bool s_time_synced = false;

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

void insertCandidate(Candidate* arr, size_t* count, const char* name, float az,
                     float el, float dist) {
  if (*count < kMaxVisible) {
    size_t i = *count;
    while (i > 0 && arr[i - 1].el < el) {
      arr[i] = arr[i - 1];
      --i;
    }
    strncpy(arr[i].name, name, sizeof(arr[i].name) - 1);
    arr[i].name[sizeof(arr[i].name) - 1] = '\0';
    arr[i].az = az;
    arr[i].el = el;
    arr[i].dist = dist;
    ++(*count);
    return;
  }

  if (el > arr[*count - 1].el) {
    size_t i = *count - 1;
    while (i > 0 && arr[i - 1].el < el) {
      arr[i] = arr[i - 1];
      --i;
    }
    strncpy(arr[i].name, name, sizeof(arr[i].name) - 1);
    arr[i].name[sizeof(arr[i].name) - 1] = '\0';
    arr[i].az = az;
    arr[i].el = el;
    arr[i].dist = dist;
  }
}

bool ensureTimeSynced() {
  if (s_time_synced) {
    return true;
  }
  return syncTime();
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t visibleCount() { return s_visible_count; }

const Satellite* visibleList() { return s_visible; }

bool syncTime() {
  configTime(0, 0, config::kNtpServer);

  const unsigned long deadline = millis() + 10000;
  time_t now = time(nullptr);
  while (now < kMinSaneUnixTime && millis() < deadline) {
    pollNetwork();
    delay(50);
    now = time(nullptr);
  }

  s_time_synced = (now >= kMinSaneUnixTime);
  Serial.println(s_time_synced ? "satellites: time synced"
                               : "satellites: time sync failed");
  return s_time_synced;
}

bool refreshTleCatalog() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, config::kTleGroupUrl)) {
    Serial.println("satellites: http.begin failed");
    return false;
  }
  http.setTimeout(kRequestTimeoutMs);

  const int code = performGetWithPoll(http);
  if (code != HTTP_CODE_OK) {
    Serial.printf("satellites: HTTP %d\n", code);
    http.end();
    return false;
  }

  String payload;
  if (!readResponseBodyWithPoll(http, payload)) {
    Serial.println("satellites: empty response");
    http.end();
    return false;
  }
  http.end();

  size_t count = 0;
  char name_buf[25] = {0};

  int line_start = 0;
  const int len = payload.length();

  while (line_start < len && count < kMaxCatalog) {
    int nl = payload.indexOf('\n', line_start);
    if (nl < 0) {
      nl = len;
    }
    String line = payload.substring(line_start, nl);
    line.trim();
    line_start = nl + 1;

    if (line.length() == 0) {
      continue;
    }

    if (line.startsWith("1 ")) {
      strncpy(s_catalog[count].name, name_buf, sizeof(s_catalog[count].name) - 1);
      s_catalog[count].name[sizeof(s_catalog[count].name) - 1] = '\0';
      line.toCharArray(s_catalog[count].line1, sizeof(s_catalog[count].line1));
      continue;
    }
    if (line.startsWith("2 ")) {
      line.toCharArray(s_catalog[count].line2, sizeof(s_catalog[count].line2));
      ++count;
      continue;
    }

    line.toCharArray(name_buf, sizeof(name_buf));
  }

  s_catalog_count = count;
  Serial.printf("satellites: catalog cached, %u entries\n",
               static_cast<unsigned>(count));
  return count > 0;
}

bool recomputePositions(double observer_lat, double observer_lon,
                        double observer_alt_m) {
  if (!ensureTimeSynced()) {
    return false;
  }
  if (s_catalog_count == 0) {
    Serial.println("satellites: no catalog cached yet");
    return false;
  }

  const unsigned long now_unix = static_cast<unsigned long>(time(nullptr));

  Candidate top[kMaxVisible];
  size_t top_count = 0;

  for (size_t i = 0; i < s_catalog_count; ++i) {
    Sgp4 sat;
    sat.site(observer_lat, observer_lon, observer_alt_m);
    sat.init(s_catalog[i].name, s_catalog[i].line1, s_catalog[i].line2);
    sat.findsat(now_unix);

    if (sat.satEl >= config::kSatelliteMinElevationDeg) {
      insertCandidate(top, &top_count, s_catalog[i].name, sat.satAz, sat.satEl,
                      sat.satDist);
    }
  }

  s_visible_count = top_count;
  for (size_t i = 0; i < top_count; ++i) {
    strncpy(s_visible[i].name, top[i].name, sizeof(s_visible[i].name) - 1);
    s_visible[i].name[sizeof(s_visible[i].name) - 1] = '\0';
    s_visible[i].azimuth_deg = top[i].az;
    s_visible[i].elevation_deg = top[i].el;
    s_visible[i].range_km = top[i].dist;
  }

  return true;
}

}  // namespace services::satellites
