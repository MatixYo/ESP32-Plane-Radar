#include "services/route_fetcher.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

#include <cstring>

#include "ui/radar_range.h"

namespace services::route {

namespace {

constexpr size_t kCacheSize = 64;
constexpr int kMaxCallsignLen = 9;
constexpr int kMaxRouteLen = 48;

struct CacheEntry {
  char callsign[kMaxCallsignLen];
  char route_iata[20];
  char route_name[kMaxRouteLen];
  uint32_t last_used;
  bool is_empty;
};

CacheEntry s_cache[kCacheSize];
SemaphoreHandle_t s_cache_mutex = nullptr;

QueueHandle_t s_queue = nullptr;
TaskHandle_t s_task = nullptr;

void initCache() {
  for (size_t i = 0; i < kCacheSize; ++i) {
    s_cache[i].is_empty = true;
  }
}

CacheEntry* findEntry(const char* callsign) {
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (!s_cache[i].is_empty && strcmp(s_cache[i].callsign, callsign) == 0) {
      return &s_cache[i];
    }
  }
  return nullptr;
}

void putCache(const char* callsign, const char* route_iata, const char* route_name) {
  if (!s_cache_mutex || xSemaphoreTake(s_cache_mutex, portMAX_DELAY) != pdTRUE) return;
  CacheEntry* entry = findEntry(callsign);
  if (entry) {
    strncpy(entry->route_iata, route_iata, sizeof(entry->route_iata) - 1);
    entry->route_iata[sizeof(entry->route_iata) - 1] = '\0';
    strncpy(entry->route_name, route_name, sizeof(entry->route_name) - 1);
    entry->route_name[sizeof(entry->route_name) - 1] = '\0';
    entry->last_used = millis();
    xSemaphoreGive(s_cache_mutex);
    return;
  }

  CacheEntry* best = &s_cache[0];
  for (size_t i = 0; i < kCacheSize; ++i) {
    if (s_cache[i].is_empty) {
      best = &s_cache[i];
      break;
    }
    if (s_cache[i].last_used < best->last_used) {
      best = &s_cache[i];
    }
  }

  strncpy(best->callsign, callsign, kMaxCallsignLen - 1);
  best->callsign[kMaxCallsignLen - 1] = '\0';
  strncpy(best->route_iata, route_iata, sizeof(best->route_iata) - 1);
  best->route_iata[sizeof(best->route_iata) - 1] = '\0';
  strncpy(best->route_name, route_name, sizeof(best->route_name) - 1);
  best->route_name[sizeof(best->route_name) - 1] = '\0';
  best->last_used = millis();
  best->is_empty = false;
  xSemaphoreGive(s_cache_mutex);
}

void fetchTask(void* pvParameters) {
  char callsign[kMaxCallsignLen];

  while (true) {
    if (xQueueReceive(s_queue, callsign, portMAX_DELAY) == pdTRUE) {
      String url = "https://api.adsbdb.com/v0/callsign/";
      url += callsign;

      NetworkClientSecure client;
      client.setInsecure(); 

      HTTPClient http;
      http.setTimeout(5000);
      if (http.begin(client, url)) {
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
          String payload = http.getString();
          JsonDocument doc;
          if (!deserializeJson(doc, payload)) {
            JsonObject orig_obj = doc["response"]["flightroute"]["origin"];
            JsonObject dest_obj = doc["response"]["flightroute"]["destination"];

            auto getBestName = [](JsonObject obj) -> const char* {
              if (obj["municipality"].is<const char*>() && strlen(obj["municipality"].as<const char*>()) > 0) {
                return obj["municipality"].as<const char*>();
              }
              if (obj["iata_code"].is<const char*>() && strlen(obj["iata_code"].as<const char*>()) > 0) {
                return obj["iata_code"].as<const char*>();
              }
              if (obj["name"].is<const char*>() && strlen(obj["name"].as<const char*>()) > 0) {
                return obj["name"].as<const char*>();
              }
              return nullptr;
            };

            const char* origin_name = getBestName(orig_obj);
            const char* dest_name = getBestName(dest_obj);
            const char* origin_iata = orig_obj["iata_code"].is<const char*>() ? orig_obj["iata_code"].as<const char*>() : nullptr;
            const char* dest_iata = dest_obj["iata_code"].is<const char*>() ? dest_obj["iata_code"].as<const char*>() : nullptr;
            
            if (origin_name && dest_name) {
              char r_iata[20];
              snprintf(r_iata, sizeof(r_iata), "%s-%s", origin_iata ? origin_iata : "???", dest_iata ? dest_iata : "???");
              char r_name[kMaxRouteLen];
              snprintf(r_name, sizeof(r_name), "%s - %s", origin_name, dest_name);
              
              putCache(callsign, r_iata, r_name);
              Serial.printf("route_fetcher: Resolved %s -> %s\n", callsign, r_name);
            } else {
              putCache(callsign, "", "");
              Serial.printf("route_fetcher: No route found for %s\n", callsign);
            }
          } else {
            putCache(callsign, "", "");
            Serial.printf("route_fetcher: JSON parse failed for %s\n", callsign);
          }
        } else if (code == 404 || code == 400) {
          putCache(callsign, "", "");
          Serial.printf("route_fetcher: HTTP %d for %s\n", code, callsign);
        } else {
          Serial.printf("route_fetcher: HTTP %d error for %s\n", code, callsign);
        }
        http.end();
      } else {
        Serial.printf("route_fetcher: http.begin failed for %s\n", callsign);
      }
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

}  // namespace

void init() {
  initCache();
  s_cache_mutex = xSemaphoreCreateMutex();
  s_queue = xQueueCreate(20, kMaxCallsignLen);
  if (s_queue && s_cache_mutex) {
    xTaskCreate(fetchTask, "RouteFetcher", 10240, nullptr, 1, &s_task);
  }
}

bool getRoute(const char* callsign, char* route_out, int route_len) {
  if (callsign == nullptr || callsign[0] == '\0') {
    return false;
  }
  
  route_out[0] = '\0';
  ui::radar::AirportDataMode mode = ui::radar::getAirportDataMode();
  if (mode == ui::radar::AirportDataMode::NONE) {
    return false;
  }

  bool cached = false;
  if (s_cache_mutex && xSemaphoreTake(s_cache_mutex, portMAX_DELAY) == pdTRUE) {
    CacheEntry* entry = findEntry(callsign);
    if (entry) {
      entry->last_used = millis();
      if (mode == ui::radar::AirportDataMode::IATA) {
        strncpy(route_out, entry->route_iata, route_len - 1);
      } else {
        strncpy(route_out, entry->route_name, route_len - 1);
      }
      route_out[route_len - 1] = '\0';
      cached = true;
    }
    xSemaphoreGive(s_cache_mutex);
  }

  if (!cached) {
    if (s_queue) {
      char trimmed[kMaxCallsignLen];
      strncpy(trimmed, callsign, kMaxCallsignLen - 1);
      trimmed[kMaxCallsignLen - 1] = '\0';
      for (int i = strlen(trimmed) - 1; i >= 0; --i) {
        if (trimmed[i] == ' ') trimmed[i] = '\0';
        else break;
      }
      xQueueSend(s_queue, trimmed, 0);
    }
  }

  return cached;
}

}  // namespace services::route
