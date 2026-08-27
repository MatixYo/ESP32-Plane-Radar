#include "services/flight_route.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cctype>
#include <cstring>

#include "config.h"
#include "data/city_exonyms.h"

namespace services::route {

namespace {

constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 8000;

struct CacheEntry {
  char callsign[9] = {0};
  char origin[20] = {0};
  char dest[20] = {0};
  char airline[24] = {0};      // operator name, ASCII-folded ("" if unknown)
  bool has_route = false;      // false = negative (no route for this callsign)
  bool soft_fail = false;      // negative only because the lookup itself failed
  bool occupied = false;
  unsigned long written_ms = 0;  // for negative-entry TTL
  unsigned long used_ms = 0;     // for LRU eviction
};

CacheEntry s_cache[config::kRouteCacheSize];

void pollHook(PollFn poll) {
  if (poll != nullptr) {
    poll();
  }
}

// --- callsign classification -------------------------------------------------

bool looksLikeAirlineCallsign(const char* cs) {
  const size_t n = strlen(cs);
  if (n < 4 || n > 8) {
    return false;
  }
  // adsb_client falls back to the lower-case 6-hex ICAO address when no
  // "flight" field is present — those never have a route.
  if (n == 6) {
    bool all_hex = true;
    for (size_t i = 0; i < 6; ++i) {
      const char c = cs[i];
      const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      if (!hex) {
        all_hex = false;
        break;
      }
    }
    if (all_hex) {
      return false;
    }
  }
  bool letter_prefix = false;
  for (size_t i = 0; i < 3 && i < n; ++i) {
    if (isalpha(static_cast<unsigned char>(cs[i]))) {
      letter_prefix = true;
    }
  }
  bool has_digit = false;
  for (size_t i = 0; i < n; ++i) {
    if (isdigit(static_cast<unsigned char>(cs[i]))) {
      has_digit = true;
    }
  }
  return letter_prefix && has_digit;
}

// --- cache -----------------------------------------------------------------

CacheEntry* findEntry(const char* cs) {
  for (auto& e : s_cache) {
    if (e.occupied && strcmp(e.callsign, cs) == 0) {
      return &e;
    }
  }
  return nullptr;
}

CacheEntry* claimSlot() {
  CacheEntry* victim = &s_cache[0];
  for (auto& e : s_cache) {
    if (!e.occupied) {
      return &e;
    }
    if (e.used_ms < victim->used_ms) {
      victim = &e;
    }
  }
  return victim;
}

// --- city name -> Italian --------------------------------------------------

// Map a UTF-8 Latin letter (À..ž range) to its ASCII base; 0 if unknown.
char latinToAscii(uint8_t b1, uint8_t b2) {
  if (b1 == 0xC3) {  // Latin-1 Supplement: À (0x80) .. ÿ (0xBF)
    static const char kMap[] =
        "AAAAAAECEEEEIIIIDNOOOOOxOUUUUYPs"   // 0x80..0x9F ('x' = ×, 'P' = Þ)
        "aaaaaaeceeeeiiiidnooooo/ouuuuypy";  // 0xA0..0xBF ('/' = ÷)
    if (b2 >= 0x80 && b2 <= 0xBF) {
      return kMap[b2 - 0x80];
    }
    return 0;
  }
  if (b1 == 0xC4 || b1 == 0xC5) {  // Latin Extended-A (common Slavic/Nordic)
    const int cp = ((b1 & 0x1F) << 6) | (b2 & 0x3F);
    switch (cp) {
      case 0x100: case 0x102: case 0x104: return 'A';
      case 0x101: case 0x103: case 0x105: return 'a';
      case 0x106: case 0x108: case 0x10A: case 0x10C: return 'C';
      case 0x107: case 0x109: case 0x10B: case 0x10D: return 'c';
      case 0x10E: case 0x110: return 'D';
      case 0x10F: case 0x111: return 'd';
      case 0x112: case 0x114: case 0x116: case 0x118: case 0x11A: return 'E';
      case 0x113: case 0x115: case 0x117: case 0x119: case 0x11B: return 'e';
      case 0x11C: case 0x11E: case 0x120: case 0x122: return 'G';
      case 0x11D: case 0x11F: case 0x121: case 0x123: return 'g';
      case 0x128: case 0x12A: case 0x12C: case 0x12E: case 0x130: return 'I';
      case 0x129: case 0x12B: case 0x12D: case 0x12F: case 0x131: return 'i';
      case 0x139: case 0x13B: case 0x13D: case 0x141: return 'L';
      case 0x13A: case 0x13C: case 0x13E: case 0x142: return 'l';
      case 0x143: case 0x145: case 0x147: return 'N';
      case 0x144: case 0x146: case 0x148: return 'n';
      case 0x14C: case 0x14E: case 0x150: return 'O';
      case 0x14D: case 0x14F: case 0x151: return 'o';
      case 0x154: case 0x156: case 0x158: return 'R';
      case 0x155: case 0x157: case 0x159: return 'r';
      case 0x15A: case 0x15C: case 0x15E: case 0x160: return 'S';
      case 0x15B: case 0x15D: case 0x15F: case 0x161: return 's';
      case 0x162: case 0x164: case 0x166: return 'T';
      case 0x163: case 0x165: case 0x167: return 't';
      case 0x168: case 0x16A: case 0x16C: case 0x16E: case 0x170: case 0x172:
        return 'U';
      case 0x169: case 0x16B: case 0x16D: case 0x16F: case 0x171: case 0x173:
        return 'u';
      case 0x179: case 0x17B: case 0x17D: return 'Z';
      case 0x17A: case 0x17C: case 0x17E: return 'z';
      case 0x177: case 0x176: return 'y';
      default: return 0;
    }
  }
  return 0;
}

// Fold a UTF-8 string to ASCII: Latin letters lose their diacritics, non-Latin
// multibyte runs are dropped. With `lower` the result is also lower-cased (for
// exonym-table matching); without it, case and spacing are preserved (for
// display of a city that has no exonym).
void foldAscii(const char* in, char* out, size_t out_len, bool lower) {
  size_t o = 0;
  if (out_len == 0) {
    return;
  }
  for (size_t i = 0; in[i] != '\0' && o + 1 < out_len;) {
    const uint8_t c = static_cast<uint8_t>(in[i]);
    char ch = 0;
    if (c < 0x80) {
      ch = static_cast<char>(c);
      ++i;
    } else if ((c & 0xE0) == 0xC0 && in[i + 1] != '\0') {
      ch = latinToAscii(c, static_cast<uint8_t>(in[i + 1]));
      i += 2;
    } else if ((c & 0xF0) == 0xE0) {
      i += 3;  // skip 3-byte sequences (non-Latin)
      continue;
    } else if ((c & 0xF8) == 0xF0) {
      i += 4;
      continue;
    } else {
      ++i;
      continue;
    }
    if (ch == 0) {
      continue;
    }
    out[o++] = lower ? static_cast<char>(tolower(static_cast<unsigned char>(ch)))
                     : ch;
  }
  out[o] = '\0';
}

}  // namespace

void normalizeCity(const char* in, char* out, size_t out_len) {
  foldAscii(in, out, out_len, /*lower=*/true);
}

namespace {

const char* italianForCity(const char* municipality) {
  if (municipality == nullptr || municipality[0] == '\0') {
    return nullptr;
  }
  char norm[40];
  normalizeCity(municipality, norm, sizeof(norm));
  for (size_t i = 0; i < data::city_exonyms::kExonymCount; ++i) {
    if (strcmp(norm, data::city_exonyms::kExonyms[i].en) == 0) {
      return data::city_exonyms::kExonyms[i].it;
    }
  }
  return nullptr;
}

// Fill `out` for one route endpoint. Preference: Italian exonym (Londra),
// then the city's own name ASCII-folded (Malaga, Katowice, Bastia), and only
// the IATA code (AGP) when adsbdb gives no city.
void formatEndpoint(JsonObjectConst ep, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || ep.isNull()) {
    return;
  }
  const char* muni = ep["municipality"].as<const char*>();
  const char* it = italianForCity(muni);
  if (it != nullptr) {
    strncpy(out, it, out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  if (muni != nullptr && muni[0] != '\0') {
    foldAscii(muni, out, out_len, /*lower=*/false);
    if (out[0] != '\0') {
      return;
    }
  }
  const char* iata = ep["iata_code"].as<const char*>();
  if (iata != nullptr && iata[0] != '\0') {
    strncpy(out, iata, out_len - 1);
    out[out_len - 1] = '\0';
  }
}

// Returns the HTTP status code (0 on transport failure). The body is read for
// 200 and for 404 — adsbdb answers an unknown callsign with 404 + a valid
// {"response":"unknown callsign"} JSON body, which is a firm "no route".
int httpGetBody(const String& url, String& payload, PollFn poll) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    return 0;
  }
  // adsbdb is also fronted by a CDN that chunks HTTP/1.1 responses without a
  // Content-Length; force HTTP/1.0 so the body arrives unframed (same reason
  // as adsb_client).
  http.useHTTP10(true);
  http.setConnectTimeout(kConnectAttemptMs);
  http.setTimeout(kRequestTimeoutMs);

  pollHook(poll);
  const int code = http.GET();
  if (code != HTTP_CODE_OK && code != HTTP_CODE_NOT_FOUND) {
    http.end();
    return code > 0 ? code : 0;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    return 0;
  }

  uint8_t buf[512];
  const unsigned long deadline = millis() + kRequestTimeoutMs;
  while (millis() < deadline) {
    pollHook(poll);
    const int avail = stream->available();
    if (avail > 0) {
      const int want =
          avail > static_cast<int>(sizeof(buf)) ? static_cast<int>(sizeof(buf))
                                                : avail;
      const int got = stream->readBytes(buf, want);
      if (got > 0) {
        payload.concat(reinterpret_cast<const char*>(buf),
                       static_cast<unsigned>(got));
      }
    } else if (!http.connected()) {
      break;
    }
    delay(1);
  }
  http.end();
  return payload.length() > 0 ? code : 0;
}

}  // namespace

Result resolve(const char* callsign, char* origin, size_t origin_len,
               char* dest, size_t dest_len, char* airline, size_t airline_len,
               PollFn poll, bool allow_network) {
  if (origin_len > 0) {
    origin[0] = '\0';
  }
  if (dest_len > 0) {
    dest[0] = '\0';
  }
  if (airline_len > 0) {
    airline[0] = '\0';
  }
  if (!config::kRouteLookupEnabled || callsign == nullptr ||
      callsign[0] == '\0') {
    return Result::kSkippedBadCallsign;
  }
  if (!looksLikeAirlineCallsign(callsign)) {
    return Result::kSkippedBadCallsign;
  }

  const unsigned long now = millis();

  CacheEntry* entry = findEntry(callsign);
  if (entry != nullptr) {
    const unsigned long ttl = entry->soft_fail ? config::kRouteRetryTtlMs
                                               : config::kRouteNegativeTtlMs;
    const bool negative_expired =
        !entry->has_route && (now - entry->written_ms) >= ttl;
    if (!negative_expired) {
      entry->used_ms = now;
      if (entry->has_route) {
        strncpy(origin, entry->origin, origin_len ? origin_len - 1 : 0);
        strncpy(dest, entry->dest, dest_len ? dest_len - 1 : 0);
        strncpy(airline, entry->airline, airline_len ? airline_len - 1 : 0);
        if (origin_len) origin[origin_len - 1] = '\0';
        if (dest_len) dest[dest_len - 1] = '\0';
        if (airline_len) airline[airline_len - 1] = '\0';
      }
      return Result::kFromCache;
    }
  }

  if (!allow_network) {
    return Result::kSkippedNoBudget;
  }

  String url = config::kRouteApiBase;
  url += callsign;

  String payload;
  const int code = httpGetBody(url, payload, poll);

  char new_origin[sizeof(CacheEntry::origin)] = {0};
  char new_dest[sizeof(CacheEntry::dest)] = {0};
  char new_airline[sizeof(CacheEntry::airline)] = {0};
  bool parsed = false;  // got a well-formed response (route or "unknown")

  if (code != 0) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      parsed = true;
      JsonObjectConst fr = doc["response"]["flightroute"].as<JsonObjectConst>();
      if (!fr.isNull()) {
        formatEndpoint(fr["origin"].as<JsonObjectConst>(), new_origin,
                       sizeof(new_origin));
        formatEndpoint(fr["destination"].as<JsonObjectConst>(), new_dest,
                       sizeof(new_dest));
        const char* al_name = fr["airline"]["name"].as<const char*>();
        if (al_name != nullptr && al_name[0] != '\0') {
          foldAscii(al_name, new_airline, sizeof(new_airline), /*lower=*/false);
        }
      }
    }
  }

  const bool has_route =
      new_origin[0] != '\0' || new_dest[0] != '\0' || new_airline[0] != '\0';

  // Always cache the outcome so a callsign is not re-queried every poll. A
  // parsed answer (route, or a firm "no route") holds for kRouteNegativeTtlMs;
  // a soft failure (timeout, TLS OOM, unparseable body) is retried sooner.
  if (entry == nullptr) {
    entry = claimSlot();
  }
  entry->occupied = true;
  strncpy(entry->callsign, callsign, sizeof(entry->callsign) - 1);
  entry->callsign[sizeof(entry->callsign) - 1] = '\0';
  strcpy(entry->origin, new_origin);
  strcpy(entry->dest, new_dest);
  strcpy(entry->airline, new_airline);
  entry->has_route = has_route;
  entry->soft_fail = !parsed;
  entry->written_ms = now;
  entry->used_ms = now;

  if (has_route) {
    strncpy(origin, new_origin, origin_len ? origin_len - 1 : 0);
    strncpy(dest, new_dest, dest_len ? dest_len - 1 : 0);
    strncpy(airline, new_airline, airline_len ? airline_len - 1 : 0);
    if (origin_len) origin[origin_len - 1] = '\0';
    if (dest_len) dest[dest_len - 1] = '\0';
    if (airline_len) airline[airline_len - 1] = '\0';
  }
  Serial.printf("route: %s [%s] -> %s > %s (http %d%s)\n", callsign,
                new_airline[0] ? new_airline : "-",
                new_origin[0] ? new_origin : "?", new_dest[0] ? new_dest : "?",
                code, parsed ? "" : ", unparsed");
  return Result::kFetched;
}

}  // namespace services::route
