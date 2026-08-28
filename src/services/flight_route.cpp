#include "services/flight_route.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cctype>
#include <cmath>
#include <cstring>

#include "config.h"
#include "data/airports.h"
#include "data/city_exonyms.h"

namespace services::route {

namespace {

constexpr int kConnectAttemptMs = 200;
constexpr unsigned long kRequestTimeoutMs = 8000;

struct CacheEntry {
  char callsign[9] = {0};
  char origin[20] = {0};       // full label: city / exonym / IATA / ICAO
  char dest[20] = {0};
  char origin_code[5] = {0};   // short label: IATA, else ICAO
  char dest_code[5] = {0};
  char airline[24] = {0};      // operator name, ASCII-folded ("" if unknown)
  float o_lat = NAN;           // route endpoint coords (NAN = airport unknown),
  float o_lon = NAN;           // used for the corridor sanity check
  float d_lat = NAN;
  float d_lon = NAN;
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

// --- airport table -------------------------------------------------------

// Binary search of the ICAO-sorted data::airports::kAirports table.
const data::airports::Airport* airportByIcao(const char* icao) {
  if (icao == nullptr || strlen(icao) != 4) {
    return nullptr;
  }
  size_t lo = 0, hi = data::airports::kAirportCount;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const int cmp = strcmp(icao, data::airports::kAirports[mid].icao);
    if (cmp == 0) {
      return &data::airports::kAirports[mid];
    }
    if (cmp < 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return nullptr;
}

// Label for one route endpoint. Preference: Italian exonym (Londra), then the
// city's own name (Malaga), then the IATA code (AGP), then the ICAO code.
void labelForAirport(const data::airports::Airport* ap, const char* icao,
                     char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }
  if (ap != nullptr) {
    const char* it = italianForCity(ap->city);
    if (it != nullptr) {
      strncpy(out, it, out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
    if (ap->city[0] != '\0') {
      foldAscii(ap->city, out, out_len, /*lower=*/false);
      if (out[0] != '\0') {
        return;
      }
    }
    if (ap->iata[0] != '\0') {
      strncpy(out, ap->iata, out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }
  if (icao != nullptr && icao[0] != '\0') {
    strncpy(out, icao, out_len - 1);
    out[out_len - 1] = '\0';
  }
}

// Short code for one route endpoint: IATA (e.g. "FCO"), else the ICAO code.
void codeForAirport(const data::airports::Airport* ap, const char* icao,
                    char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }
  if (ap != nullptr && ap->iata[0] != '\0') {
    strncpy(out, ap->iata, out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  if (icao != nullptr && icao[0] != '\0') {
    strncpy(out, icao, out_len - 1);
    out[out_len - 1] = '\0';
  }
}

// --- corridor sanity check --------------------------------------------------

double toRad(double d) { return d * (M_PI / 180.0); }

// Angular distance (radians) between two lat/lon points — haversine.
double angularDist(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = toRad(lat1), p2 = toRad(lat2);
  const double dp = toRad(lat2 - lat1), dl = toRad(lon2 - lon1);
  const double a = sin(dp / 2) * sin(dp / 2) +
                   cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

double initialBearing(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = toRad(lat1), p2 = toRad(lat2), dl = toRad(lon2 - lon1);
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  return atan2(y, x);
}

// How far (km) the aircraft at P lies outside the corridor around the great
// circle from A to B: the cross-track distance, widened when P sits beyond
// either endpoint along the track. ~0 for a plane genuinely en route.
float corridorDistKm(float plat, float plon, float alat, float alon,
                     float blat, float blon) {
  constexpr double R = 6371.0;
  const double d13 = angularDist(alat, alon, plat, plon);
  const double t13 = initialBearing(alat, alon, plat, plon);
  const double t12 = initialBearing(alat, alon, blat, blon);
  const double leg = R * angularDist(alat, alon, blat, blon);

  double off = fabs(asin(sin(d13) * sin(t13 - t12))) * R;  // cross-track
  const double along = R * d13 * cos(t13 - t12);           // along-track from A
  if (along < 0.0) {
    off = fmax(off, -along);
  } else if (along > leg) {
    off = fmax(off, along - leg);
  }
  return static_cast<float>(off);
}

// True when the route may be shown: no coords to check against, the check is
// disabled, or the aircraft is within the corridor.
bool routeFitsPosition(const CacheEntry& e, float ac_lat, float ac_lon) {
  if (config::kRouteCorridorMaxKm <= 0.0f) {
    return true;
  }
  if (isnan(e.o_lat) || isnan(e.o_lon) || isnan(e.d_lat) || isnan(e.d_lon)) {
    return true;  // an endpoint airport is not in the table — cannot judge
  }
  if (isnan(ac_lat) || isnan(ac_lon) || (ac_lat == 0.0f && ac_lon == 0.0f)) {
    return true;
  }
  return corridorDistKm(ac_lat, ac_lon, e.o_lat, e.o_lon, e.d_lat, e.d_lon) <=
         config::kRouteCorridorMaxKm;
}

// Copy a cached hit into the caller's buffers. The airline always comes
// through; origin/dest are withheld when the aircraft is nowhere near the
// corridor between them (stale callsign reuse). `full_names` picks between
// the resolved city/exonym label and the short IATA/ICAO code.
void fillFromCache(const CacheEntry& e, float ac_lat, float ac_lon,
                   bool full_names, char* origin, size_t origin_len, char* dest,
                   size_t dest_len, char* airline, size_t airline_len) {
  if (airline_len) {
    strncpy(airline, e.airline, airline_len - 1);
    airline[airline_len - 1] = '\0';
  }
  if (!routeFitsPosition(e, ac_lat, ac_lon)) {
    return;
  }
  const char* origin_src = full_names ? e.origin : e.origin_code;
  const char* dest_src = full_names ? e.dest : e.dest_code;
  if (origin_len) {
    strncpy(origin, origin_src, origin_len - 1);
    origin[origin_len - 1] = '\0';
  }
  if (dest_len) {
    strncpy(dest, dest_src, dest_len - 1);
    dest[dest_len - 1] = '\0';
  }
}

// --- HTTP ----------------------------------------------------------------

// Returns the HTTP status code (0 on transport failure). The body is read for
// 200 and for 404 — both hexdb ("Route not found.") and adsbdb ("unknown
// callsign") answer a miss with 404 plus a valid JSON body, a firm "no route".
int httpGetBody(const String& url, String& payload, PollFn poll) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    return 0;
  }
  // hexdb and adsbdb both sit behind a CDN that chunks HTTP/1.1 responses with
  // no Content-Length; force HTTP/1.0 so the body arrives unframed (same reason
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

// --- hexdb route parsing -------------------------------------------------

// Pull the first and last ICAO codes out of a hexdb "route" value
// ("EBCI-LIME", or "A-B-C" for a multi-leg entry). Returns false when the
// value holds no usable code.
bool splitRoute(const char* route, char origin_icao[5], char dest_icao[5]) {
  origin_icao[0] = '\0';
  dest_icao[0] = '\0';
  if (route == nullptr) {
    return false;
  }
  char first[5] = {0};
  char last[5] = {0};
  size_t tok_len = 0;
  char tok[5] = {0};
  for (const char* p = route;; ++p) {
    if (*p != '\0' && *p != '-') {
      if (tok_len < 4) {
        tok[tok_len] = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
      }
      ++tok_len;
    } else {
      if (tok_len == 4) {
        tok[4] = '\0';
        if (first[0] == '\0') {
          strcpy(first, tok);
        }
        strcpy(last, tok);
      }
      tok_len = 0;
      if (*p == '\0') {
        break;
      }
    }
  }
  if (first[0] == '\0') {
    return false;
  }
  strcpy(origin_icao, first);
  if (strcmp(first, last) != 0) {
    strcpy(dest_icao, last);
  }
  return true;
}

}  // namespace

Result resolve(const char* callsign, char* origin, size_t origin_len, char* dest,
               size_t dest_len, char* airline, size_t airline_len, float ac_lat,
               float ac_lon, bool full_names, PollFn poll, bool allow_network) {
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
        fillFromCache(*entry, ac_lat, ac_lon, full_names, origin, origin_len,
                      dest, dest_len, airline, airline_len);
      }
      return Result::kFromCache;
    }
  }

  if (!allow_network) {
    return Result::kSkippedNoBudget;
  }

  // --- hexdb: callsign -> "ICAO-ICAO" ---
  String url = config::kRouteApiBase;
  url += callsign;

  String payload;
  const int code = httpGetBody(url, payload, poll);

  char new_origin[sizeof(CacheEntry::origin)] = {0};
  char new_dest[sizeof(CacheEntry::dest)] = {0};
  char new_origin_code[sizeof(CacheEntry::origin_code)] = {0};
  char new_dest_code[sizeof(CacheEntry::dest_code)] = {0};
  char new_airline[sizeof(CacheEntry::airline)] = {0};
  float o_lat = NAN, o_lon = NAN, d_lat = NAN, d_lon = NAN;
  bool parsed = false;  // got a well-formed response (route or "not found")

  if (code != 0) {
    JsonDocument doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      parsed = true;
      const char* route = doc["route"].as<const char*>();
      char oi[5], di[5];
      if (route != nullptr && splitRoute(route, oi, di)) {
        const data::airports::Airport* oa = airportByIcao(oi);
        const data::airports::Airport* da = airportByIcao(di);
        labelForAirport(oa, oi, new_origin, sizeof(new_origin));
        labelForAirport(da, di[0] ? di : nullptr, new_dest, sizeof(new_dest));
        codeForAirport(oa, oi, new_origin_code, sizeof(new_origin_code));
        codeForAirport(da, di[0] ? di : nullptr, new_dest_code,
                       sizeof(new_dest_code));
        if (di[0] == '\0') {
          new_dest[0] = '\0';
          new_dest_code[0] = '\0';
        }
        if (oa != nullptr) {
          o_lat = oa->lat_e7 / 1e7f;
          o_lon = oa->lon_e7 / 1e7f;
        }
        if (da != nullptr) {
          d_lat = da->lat_e7 / 1e7f;
          d_lon = da->lon_e7 / 1e7f;
        }
      }
    }
  }

  const bool has_route = new_origin[0] != '\0' || new_dest[0] != '\0';

  // --- adsbdb: airline name only (hexdb carries none) ---
  if (has_route && config::kAirlineApiBase[0] != '\0') {
    String aurl = config::kAirlineApiBase;
    aurl += callsign;
    String apayload;
    if (httpGetBody(aurl, apayload, poll) != 0) {
      JsonDocument adoc;
      if (deserializeJson(adoc, apayload) == DeserializationError::Ok) {
        const char* al_name =
            adoc["response"]["flightroute"]["airline"]["name"].as<const char*>();
        if (al_name != nullptr && al_name[0] != '\0') {
          foldAscii(al_name, new_airline, sizeof(new_airline), /*lower=*/false);
        }
      }
    }
  }

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
  strcpy(entry->origin_code, new_origin_code);
  strcpy(entry->dest_code, new_dest_code);
  strcpy(entry->airline, new_airline);
  entry->o_lat = o_lat;
  entry->o_lon = o_lon;
  entry->d_lat = d_lat;
  entry->d_lon = d_lon;
  entry->has_route = has_route;
  entry->soft_fail = !parsed;
  entry->written_ms = now;
  entry->used_ms = now;

  if (has_route) {
    fillFromCache(*entry, ac_lat, ac_lon, full_names, origin, origin_len, dest,
                  dest_len, airline, airline_len);
  }
  const bool suppressed =
      has_route && origin_len > 0 && origin[0] == '\0' && dest_len > 0 &&
      dest[0] == '\0';
  Serial.printf("route: %s [%s] -> %s > %s (http %d%s%s)\n", callsign,
                new_airline[0] ? new_airline : "-",
                new_origin[0] ? new_origin : "?", new_dest[0] ? new_dest : "?",
                code, parsed ? "" : ", unparsed",
                suppressed ? ", off-corridor" : "");
  return Result::kFetched;
}

}  // namespace services::route
