#include "services/web_server.h"

#include <WebServer.h>
#include <ArduinoJson.h>
#include <memory>

#include "ui/web_ui_html.h"
#include "services/adsb_client.h"
#include "ui/radar_range.h"
#include "services/radar_location.h"

namespace services::web {

namespace {
std::unique_ptr<WebServer> s_server;

void handleRoot() {
  if (s_server) s_server->send(200, "text/html", kWebUiHtml);
}

void handleApiPlanes() {
  const adsb::Aircraft* planes = adsb::aircraftList();
  const size_t count = adsb::aircraftCount();

  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  for (size_t i = 0; i < count; ++i) {
    JsonObject obj = array.add<JsonObject>();
    obj["callsign"] = planes[i].callsign;
    obj["type"] = planes[i].type;
    obj["alt"] = planes[i].alt;
    obj["speed"] = planes[i].speed;
    obj["route"] = planes[i].route;
    obj["lat"] = planes[i].lat;
    obj["lon"] = planes[i].lon;
    obj["track"] = planes[i].track_deg;
  }

  String json;
  serializeJson(doc, json);
  if (s_server) s_server->send(200, "application/json", json);
}

void handleApiConfigGet() {
  JsonDocument doc;
  doc["rangeIndex"] = ui::radar::rangeIndex();
  doc["airportDataMode"] = static_cast<int>(ui::radar::getAirportDataMode());
  doc["useMiles"] = ui::radar::useMiles();
  doc["centerLat"] = services::location::lat();
  doc["centerLon"] = services::location::lon();
  
  String json;
  serializeJson(doc, json);
  if (s_server) s_server->send(200, "application/json", json);
}

void handleApiConfigPost() {
  if (!s_server) return;
  if (s_server->hasArg("plain") == false) {
    s_server->send(400, "text/plain", "Body not received");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, s_server->arg("plain"));
  if (error) {
    s_server->send(400, "text/plain", "Invalid JSON");
    return;
  }

  if (doc["rangeIndex"].is<uint8_t>()) {
    ui::radar::setRangeIndex(doc["rangeIndex"].as<uint8_t>());
  }
  if (doc["airportDataMode"].is<int>()) {
    ui::radar::setAirportDataMode(static_cast<ui::radar::AirportDataMode>(doc["airportDataMode"].as<int>()));
  }
  if (doc["useMiles"].is<bool>()) {
    ui::radar::setUseMiles(doc["useMiles"].as<bool>());
  }
  if (doc["centerLat"].is<float>() && doc["centerLon"].is<float>()) {
    String lat_str = String(doc["centerLat"].as<float>(), 6);
    String lon_str = String(doc["centerLon"].as<float>(), 6);
    services::location::saveFromStrings(lat_str.c_str(), lon_str.c_str());
  }

  s_server->send(200, "application/json", "{\"status\":\"ok\"}");
}

}  // namespace

void init() {
  s_server = std::make_unique<WebServer>(80);
  s_server->on("/", HTTP_GET, handleRoot);
  s_server->on("/api/planes", HTTP_GET, handleApiPlanes);
  s_server->on("/api/config", HTTP_GET, handleApiConfigGet);
  s_server->on("/api/config", HTTP_POST, handleApiConfigPost);
  s_server->begin();
}

void handleClient() {
  if (s_server) s_server->handleClient();
}

}  // namespace services::web
