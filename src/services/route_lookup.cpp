#include "services/route.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cstring>

namespace services::route {

namespace {

constexpr char kApiBase[] = "https://api.adsbdb.com/v0/callsign/";
constexpr int kConnectTimeoutMs = 1500;
constexpr unsigned long kRequestTimeoutMs = 3000;

void copyField(const JsonObject& obj, const char* key, char* out, size_t len) {
  out[0] = '\0';
  if (obj[key].is<const char*>()) {
    strncpy(out, obj[key].as<const char*>(), len - 1);
    out[len - 1] = '\0';
  }
}

void copyCode(const JsonObject& airport, char* out, size_t len) {
  copyField(airport, "iata_code", out, len);
  if (out[0] == '\0') {
    copyField(airport, "icao_code", out, len);
  }
}

}  // namespace

bool lookup(const char* callsign, RouteInfo* out) {
  out->valid = false;
  out->origin_code[0] = '\0';
  out->origin_city[0] = '\0';
  out->dest_code[0] = '\0';
  out->dest_city[0] = '\0';
  if (callsign == nullptr || callsign[0] == '\0') {
    return false;
  }

  String url = kApiBase;
  url += callsign;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kRequestTimeoutMs);
  if (!http.begin(client, url)) {
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("route: HTTP %d for %s\n", code, callsign);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    return false;
  }
  JsonObject route = doc["response"]["flightroute"].as<JsonObject>();
  if (route.isNull()) {
    return false;
  }
  JsonObject origin = route["origin"].as<JsonObject>();
  JsonObject dest = route["destination"].as<JsonObject>();
  if (origin.isNull() || dest.isNull()) {
    return false;
  }

  copyCode(origin, out->origin_code, sizeof(out->origin_code));
  copyField(origin, "municipality", out->origin_city, sizeof(out->origin_city));
  copyCode(dest, out->dest_code, sizeof(out->dest_code));
  copyField(dest, "municipality", out->dest_city, sizeof(out->dest_city));

  out->valid = out->origin_code[0] != '\0' || out->dest_code[0] != '\0';
  Serial.printf("route: %s  %s -> %s\n", callsign, out->origin_code,
                out->dest_code);
  return out->valid;
}

}  // namespace services::route
