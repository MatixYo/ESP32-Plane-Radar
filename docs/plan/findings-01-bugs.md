# Findings 01 — Bugs and Correctness

I read every non-generated source file under `src/` and `include/`, traced the terrain fetch state machine, the PNG decoder, the frame-compose path and the settings/portal path end to end, and ran `make test` (all three host envs green). **The headline result on the memory invariant is a negative: I could not find a code path that composes a frame while a tile is decoding.** The invariant holds by construction, for a specific reason worth recording — the poll hook `pollWifiAndTaps()` (`src/main.cpp:88`) deliberately calls only `gesture::tapPress()`, never `gesture::tapPoll()`, so no tap can be *dispatched* (and therefore no `radarDisplayDraw()` can run) from inside `HttpClient::get`. The two other draw entry points reachable from the poll hook (`statusScreenWifiReset` via `bootButtonPollLongPress`, and the WiFiManager `/paramsave` callback) write to `tft` or to NVS, never to `s_frame`'s pixel buffer, and every compose begins with `gfx.fillScreen()` so a dirty scratch region can never survive into a `pushSprite`. That `tapPress`/`tapPoll` split is load-bearing and non-obvious and deserves a comment. What I found instead is a silent permanent loss of the user's stored radar location on any repeat portal save, a retry gate that blocks the wrong view for a minute, three `millis() + timeout` absolute deadlines that break at the 49.7-day rollover, and a cluster of smaller malformed-input and no-FPU defects. Nothing I found is a memory-safety bug: I specifically tried and failed to break the Huffman table construction, the LZ77 distance bound, the scratch sizing and the `beginTile` reverse maps (see "Cleared").

| ID | Severity | file:line | One-line claim |
|----|----------|-----------|----------------|
| BUG-01 | High | `src/core/portal_params.cpp:72` | A second portal save silently overwrites the stored manual radar location with the active site's coordinates, unrecoverably. |
| BUG-02 | Medium | `src/core/terrain.cpp:391` | The terrain retry gate is keyed by range preset only, so switching *site* inherits a 60 s "no terrain" block from a different view. |
| BUG-03 | Medium | `src/platform/device/http_arduino.cpp:35` | Three absolute `millis() + timeout` deadlines fail every HTTP request and every reconnect for a 10–15 s window at each 49.7-day `millis()` rollover. |
| BUG-04 | Low | `src/platform/device/kv_nvs.cpp:60` | Every NVS write ignores its return value, so a failed persist is reported to the user as success. |
| BUG-05 | Low | `src/core/geo.cpp:9` | Longitude is not normalised across the antimeridian, so a target 20 km east of a ±180° centre is drawn as a rim dot pointing 180° the wrong way. |
| BUG-06 | Low | `src/core/adsb.cpp:258` | A partial aircraft list is published on a parse error, and the store has no staleness bound at all. |
| BUG-07 | Low | `src/core/portal_params.cpp:147` | A portal-driven centre change does not clear the ADS-B store, unlike the double-tap path, so old traffic is plotted against the new centre. |
| BUG-08 | Low | `src/platform/png_decode.cpp:179` | `skip(length + 4)` overflows uint32 on a chunk length ≥ 0xFFFFFFFC, and chunk length is never checked against PNG's 2^31−1 limit. |
| BUG-09 | Low | `src/platform/png_decode.cpp:150` | PNG height is never bounded (width is), so `PixelFn` can be handed `y > 255` despite the header calling those "tile-local coordinates". |
| BUG-10 | Low | `src/ui/runway_overlay.cpp:275` | The `s_in_range` memo caches only hits and the range test takes a `sqrtf`, costing ~1700 needless library-call square roots per frame on an FPU-less core. |
| BUG-11 | Low | `src/core/terrain.cpp:437` | `last_request_ms` is stamped before the blocking request, so the documented 250 ms inter-tile pause never actually happens. |
| BUG-12 | Low | `src/core/adsb.cpp:69` | `out[0] = '\0'` executes before the `out_len == 0` guard in two functions (unreachable from today's callers). |
| BUG-13 | Low | `src/main.cpp:102` | Turning the terrain layer off mid-download wedges `s_prog.active` / `g_terrain_download_active` at `true` permanently, because nothing ever calls `terrain::clear()`. |

---

### BUG-01 — A repeat portal save destroys the stored manual radar location  [High]

- **Location:** `src/core/portal_params.cpp:72-75` (prefill), `src/core/portal_params.cpp:147-152` (commit order), `src/core/settings.cpp:182-187` (unconditional persist)
- **Claim:** The portal prefills `radar_lat`/`radar_lon` from `settings::lat()`, which is the *active site's* coordinate, and `commit()` then writes that value into the manual-location NVS keys, so the user's own lat/lon is silently and irreversibly replaced by the site's.
- **Failure scenario:**
  1. Portal save #1, no sites: `lat=52.3676, lon=4.9041`. Persisted to `radar/lat`, `radar/lon`; `s_lat/s_lon` set.
  2. Portal save #2, user types `LOWG` into "Airport 1". `commit()` runs `saveSites` **first**: `s_site_count = 1`, `applyActiveSiteCoords()` sets `s_lat = 47.9931`. Then `saveLocationFromStrings("52.367600","4.904100")` persists 52.3676 and — because `s_site_count != 0` — correctly leaves `s_lat` alone. Still fine.
  3. Portal save #3, user only wants to tick "Show terrain". The GET renders `radar_lat` from `currentValue()` → `settings::lat()` → **47.9931** (Graz, not Amsterdam). The user changes nothing and presses Save. `commit()` → `saveLocationFromStrings("47.993100","15.439600")` → `KV::putDouble(radar/lat, 47.9931)`.
  4. The Amsterdam coordinate is gone from NVS. When the user later deletes `LOWG`, `saveSites(..,0)` restores from NVS (`settings.cpp:275-281`) and the radar lands on Graz.
- **Evidence:**
```
src/core/portal_params.cpp
 72   if (isField(field, "radar_lat")) {
 73     snprintf(buf, len, "%.6f", settings::lat());
 74   } else if (isField(field, "radar_lon")) {
 75     snprintf(buf, len, "%.6f", settings::lon());

src/core/portal_params.cpp
147   settings::saveSites(idents, n);
148
149   if (!settings::saveLocationFromStrings(s_pending_lat, s_pending_lon)) {

src/core/settings.cpp
182   KV::putDouble(kNsLocation, kKeyLat, lat_v);
183   KV::putDouble(kNsLocation, kKeyLon, lon_v);
184   if (s_site_count == 0) {
185     s_lat = lat_v;
186     s_lon = lon_v;
187   }
```
  Note the asymmetry at 182–187: the *runtime* value is guarded by `s_site_count == 0`, the *persisted* value is not.
- **Confidence:** Confirmed (traced through both portals — device via `wifi_setup_device.cpp:117`, native via `portal_server.cpp:284`; both prefill from the same `currentValue`).
- **Suggested fix:** Give `core::settings` an accessor for the *stored* manual coordinate (`storedLat()/storedLon()`) and prefill from that instead of `lat()`. No memory or FPU impact.
- **Is it pinned by an existing test?** No. `test_settings.cpp` covers `saveSites`/`parseCoord`/`validLatLon` individually but never the `commit()` interaction; there is no test for `portal_params.cpp` at all.

---

### BUG-02 — The terrain retry gate is keyed by range preset, not by view  [Medium]

- **Location:** `src/core/terrain.cpp:391-394`, gate written at `:226-229`, documented intent at `:41-46`
- **Claim:** `gateRetries()` records only `(now, range_index)`, so a centre change (site double-tap) inherits the previous view's 60-second failure gate, and nothing in production ever calls `terrain::clear()` to release it.
- **Failure scenario:** Two sites, `LOWG` and `EHAM`, at range preset 1.
  1. At `LOWG` the first tile fails three times (transient DNS/TLS — the ordinary case this logic exists for). `gateRetries(1)` sets `s_fail_ms = t0`, `s_fail_range_index = 1`, `endDownload()`.
  2. Two seconds later the user double-taps. `onSiteTap()` → centre is now `EHAM`. `main.cpp` calls `core::adsb::clear()` but **not** `core::terrain::clear()` — grep confirms `terrain::clear()` has no production caller at all, only test `setUp`.
  3. `ensureGrid(EHAM…, 1, span)`: `range_index == s_fail_range_index == 1` and `nowMs() - s_fail_ms == 2000 < 60000`, so line 393 returns `false`.
  4. `EHAM` gets no terrain for the remaining 58 s, with no log line explaining it. A single tap (range change) *would* clear it — the opposite of what the user's gesture means.
- **Evidence:**
```
src/core/terrain.cpp
 41  /**
 42   * Retry gate for the view that last failed. Keyed by preset so that tapping to
 43   * another range tries immediately instead of inheriting the gate.
 44   */
 45  unsigned long s_fail_ms = 0;
 46  uint8_t s_fail_range_index = kNoRange;
...
226  void gateRetries(uint8_t range_index) {
227    s_fail_ms = platform::nowMs();
228    s_fail_range_index = range_index;
229  }
...
391    if (s_fail_range_index == range_index &&
392        platform::nowMs() - s_fail_ms < config::kTerrainRetryIntervalMs) {
393      return false;
394    }
```
  The comment says "the view that last failed", but the key is the preset alone; the centre is part of the view and is not recorded, so the gate over-matches.
- **Confidence:** Confirmed (the unsigned rollover arithmetic at 392 is itself correct).
- **Suggested fix:** Store `s_fail_lat`/`s_fail_lon` alongside `s_fail_range_index` and compare all three with the same `kCenterEpsilonDeg` test `gridReady()` uses. 16 extra bytes of static RAM, no heap, no float work in a hot loop.
- **Is it pinned by an existing test?** No — and the closest test documents the gap: `test_the_retry_gate_belongs_to_the_view_that_failed` (`test/test_terrain_fetch/test_terrain_fetch.cpp:447`) varies only the range index (`kRange + 1`), never the centre.

---

### BUG-03 — Absolute `millis() + timeout` deadlines break at the 49.7-day rollover  [Medium]

- **Location:** `src/platform/device/http_arduino.cpp:35-36`, `:102` (deadline built at `:164`), `src/platform/device/wifi_setup_device.cpp:285-286`
- **Claim:** Three loops compute an absolute deadline as `millis() + timeout` then test `millis() < deadline`; when the addition wraps past `ULONG_MAX` the comparison is immediately false and the loop body never executes once.
- **Failure scenario:** `millis()` is 32-bit on ESP32 (`platform_device.cpp:21` returns it verbatim) and wraps every ≈49 d 17 h.
  1. Uptime reaches `millis() == 0xFFFFFF00` (256 ms before the wrap).
  2. `performGetWithPoll(http, 10000, fn)`, line 35: `deadline = 0xFFFFFF00 + 10000 = 0x2610` (9744) after wrapping.
  3. Line 36: `while (0xFFFFFF00 < 9744)` is false. `http.GET()` is never called; returns `HTTPC_ERROR_READ_TIMEOUT`.
  4. Every ADS-B fetch and terrain tile fails instantly for the last 10 s before the wrap. Terrain burns its three `kMaxTileFailures` in ~750 ms and takes the 60 s retry gate, so terrain stays off for a minute *after* the clock recovers.
  5. Same shape in `waitForLinkWithUi` (`wifi_setup_device.cpp:285`) makes each of the three 15 s reconnect attempts return instantly. The `StreamBodyReader` deadline (`:164` → `:102`) makes `fill()` return false at once, so every body reads as empty.
- **Evidence:**
```
src/platform/device/http_arduino.cpp
 33  int performGetWithPoll(HTTPClient& http, unsigned long timeout_ms, PollFn fn) {
 34    http.setConnectTimeout(kConnectAttemptMs);
 35    const unsigned long deadline = millis() + timeout_ms;
 36    while (millis() < deadline) {
...
102      while (millis() < deadline_) {
...
164    StreamBodyReader body(http, *stream, millis() + timeout_ms, fn);

src/platform/device/wifi_setup_device.cpp
284  bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
285    const unsigned long deadline = millis() + attempt_ms;
286    while (millis() < deadline) {
```
  For contrast, every timer in `src/main.cpp` and `src/core/terrain.cpp` uses the rollover-safe `now - then >= interval` form (`main.cpp:186`, `terrain.cpp:392`, `tap_gesture.cpp:17`), so this is an inconsistency inside the codebase, not house style.
- **Confidence:** Confirmed (`unsigned long` is 32-bit under riscv32 Arduino).
- **Suggested fix:** `const unsigned long start = millis(); while (millis() - start < timeout_ms)`. Same for `StreamBodyReader`: carry `start_` + `timeout_` instead of `deadline_`. No memory or FPU impact. `wifi_setup_native.cpp:97` has the same pattern but `unsigned long` is 64-bit on the host, so it is not practically reachable there — still worth fixing for symmetry.
- **Is it pinned by an existing test?** No. Host tests never build `http_arduino.cpp`, and `test_terrain_fetch` supplies its own `HttpClient::get`.

---

### BUG-04 — NVS write failures are silently swallowed and reported as success  [Low]

- **Location:** `src/platform/device/kv_nvs.cpp:57-62` and the four sibling `put*` functions (`:66-97`); consumed at `src/core/settings.cpp:68`, `:239`, `:285`, `:296`, `:313`
- **Claim:** `Preferences::putBool/putUChar/putDouble/putString` return bytes written (0 on failure) and every wrapper discards it, so `saveSites()` and the portal setters return `true` and log success even when nothing was persisted.
- **Failure scenario:** The `planeradar` NVS region fills or wears out. The user configures six airports.
  1. `saveSites` → `persistSitesString()` → `KV::putString(kNsRadar, "sites", buf)`; the inner `putString` returns 0.
  2. `kv_nvs.cpp:98` ignores it. `saveSites` returns `true` unconditionally (`settings.cpp:289`) and logs `"Airport sites saved: 6 active"`.
  3. The portal shows the sites (they are in the in-RAM `s_site_idents`), so the user believes it took.
  4. Next boot, `loadSitesFromStorage()` reads an empty string and the device silently reverts to the default coordinate.
- **Evidence:**
```
src/platform/device/kv_nvs.cpp
 94  void KeyValueStore::putString(const char* ns, const char* key,
 95                                const char* value) {
 96    Scoped p(ns, false);
 97    if (p.ok()) {
 98      (*p).putString(key, value);      // <-- size_t return discarded
 99    }
100  }

src/core/settings.cpp
287    platform::logf("Airport sites saved: %u active\n",
288                     static_cast<unsigned>(s_site_count));
289    return true;                        // <-- unconditional
```
- **Confidence:** Confirmed for the missing check. The trigger (a genuinely failing NVS write) is Plausible rather than reproduced.
- **Suggested fix:** Make the `put*` wrappers return `bool` and propagate, or at minimum `logf` on a zero return. No memory or FPU impact.
- **Is it pinned by an existing test?** No. The host store has the same shape (`kv_json_file.cpp:120` logs on failure but `put()` at `:147` returns void), so a test needs a fault-injecting store.

---

### BUG-05 — Longitude is not normalised across the antimeridian  [Low]

- **Location:** `src/core/geo.cpp:9-10`, duplicated at `src/ui/runway_overlay.cpp:75-79`
- **Claim:** `dx_km = (lon - center_lon) * 111` with no ±180 wrap, so a target a few kilometres across the antimeridian computes as ~40,000 km away and its bearing cue points the opposite way.
- **Failure scenario:** Centre manually set to `lat 0, lon 179.0` (`validLatLon` allows up to ±180). Aircraft at `lon -179.5`, i.e. 167 km due **east**.
  1. `dx_km = (-179.5 - 179.0) * 111 = -39793 km`, `dist_km ≈ 39793`.
  2. `isInsideOuterRingKm` is false, so `drawAircraft` takes the rim-dot branch (`radar_display.cpp:446`).
  3. `rimPointForDistantTarget` → `atan2f(-39793, 0)` → −90°, dot placed on the **west** rim. True bearing is 90° east. The direction cue — the entire purpose of that dot per `geo.h:58-62` — is exactly inverted.
  4. Same arithmetic in `runway_overlay.cpp:282` makes every airport across the meridian permanently out of range, so its runways never draw.
- **Evidence:**
```
src/core/geo.cpp
  7  Offset offsetKmFromCenter(const Viewport& vp, float lat, float lon) {
  8    Offset o;
  9    o.dx_km = static_cast<float>(lon - vp.center_lon) * kKmPerDeg;
 10    o.dy_km = static_cast<float>(lat - vp.center_lat) * kKmPerDeg;
```
  `core::terrain` handles this case deliberately and has a test for it (`terrain.cpp:150-156`, `test_view_across_the_antimeridian_fills_the_whole_grid`), so the projection layer is the outlier.
- **Confidence:** Confirmed for the arithmetic. Reachability is **Plausible**: no entry in the 1166-airport table is within a fetch radius of ±180° (closest large airports are ~280 km away), so today this needs a manually entered lat/lon.
- **Suggested fix:** Wrap the delta once in `offsetKmFromCenter`: `double d = lon - vp.center_lon; if (d > 180.0) d -= 360.0; else if (d < -180.0) d += 360.0;` Two comparisons per aircraft — negligible against the `sqrtf` already on the same line. Fix `runway_overlay.cpp`'s private copy the same way, or better, have it call `core::geo::offsetKmFromCenter`.
- **Is it pinned by an existing test?** No. `test_geo.cpp` covers bearings and clipping but never crosses ±180.

---

### BUG-06 — A partial aircraft list is published on a parse error, and the store never goes stale  [Low]

- **Location:** `src/core/adsb.cpp:258`
- **Claim:** `s_aircraft_count = n` is assigned on the error path as well as the success path, so a truncated response leaves a partial list live; and because nothing timestamps or expires the store, a long outage leaves minutes-old positions on screen presented as current traffic.
- **Failure scenario (a):** adsb.fi returns 40 aircraft; the connection drops after 12.
  1. `parseBody` decodes 12, then `deserializeJson` fails with `IncompleteInput`, `ok = false`, `break`.
  2. Line 258 sets `s_aircraft_count = 12`; line 259 returns `false`, so `fetchAndDrawAircraft` skips the repaint (`main.cpp:129`). Nothing wrong is *shown* yet.
  3. About a second later `maybeFetchTerrain` hits its falling edge and calls `ui::radarDisplayDraw()` (`main.cpp:120`), which reads `aircraftCount() == 12`. Twelve of forty aircraft are drawn with no indication the picture is incomplete. A single BOOT tap does the same.
- **Failure scenario (b):** adsb.fi unreachable for ten minutes. `fetchUpdate` returns `false` every 10 s; `parseBody` never runs, so the store keeps the last snapshot. Any repaint redraws ten-minute-old positions, headings and speed vectors as if live. `onSiteTap` calls `core::adsb::clear()` precisely because showing traffic against the wrong reference is wrong; the same reasoning applies to time.
- **Evidence:**
```
src/core/adsb.cpp
249      c = in.nextNonSpace();
250      if (c == ',') {
...
256    }
257
258    s_aircraft_count = n;
259    return ok;
```
- **Confidence:** Confirmed for both as written. Whether (b) is a bug or a missing feature is a judgement call; I report it because the repaint path presents it as live data.
- **Suggested fix:** (a) `s_aircraft_count = ok ? n : 0;` (or leave the previous count alone). (b) record `s_fetched_ms = nowMs()` on success and have `aircraftCount()` return 0 — or the UI grey the symbols — once `nowMs() - s_fetched_ms` exceeds a few fetch intervals. Integer work only.
- **Is it pinned by an existing test?** Partly, and the gap is visible: `test_truncated_body_is_an_error` (`test/test_adsb/test_adsb.cpp:96`) asserts only the `false` return and deliberately does not assert the count, unlike `test_missing_array_is_an_error` immediately above it, which does assert `aircraftCount() == 0`.

---

### BUG-07 — A portal-driven centre change does not clear the ADS-B store  [Low]

- **Location:** `src/core/portal_params.cpp:147`, `src/core/settings.cpp:286` (`applyActiveSiteCoords`), contrast with `src/main.cpp:53-69`
- **Claim:** `onSiteTap()` clears the aircraft store before the centre moves; the portal path reaches the same `applyActiveSiteCoords()` without doing so, so cached traffic is re-projected against the new centre.
- **Failure scenario:** Showing `LOWG` with 15 aircraft cached. The user changes "Airport 1" to `EHAM` in the LAN portal.
  1. Save callback → `core::portal::commit()` → `settings::saveSites` → `applyActiveSiteCoords()` → `s_lat/s_lon` jump 900 km.
  2. Nothing clears the 15 Graz aircraft and nothing nudges `g_last_adsb_fetch_ms`, so the next fetch can be up to 10 s away.
  3. Meanwhile `gridReady()` now fails, a terrain download starts, and its falling edge calls `ui::radarDisplayDraw()` within about a second (`main.cpp:119-121`).
  4. That frame draws Amsterdam's rings, label and terrain with Graz's aircraft — all ~900 km off, so `isInsideOuterRingKm` pushes them to the rim as a fan of dots pointing south-east: a plausible-looking but entirely fictitious traffic picture.
- **Evidence:**
```
src/main.cpp
 53  void onSiteTap() {
 54    if (core::settings::siteCount() < 2) {
 55      return;
 56    }
 57    core::settings::siteNext();
 58    core::adsb::clear();          // <-- the tap path knows to do this
...
 67    g_last_adsb_fetch_ms = pf::nowMs() - config::kAdsbFetchIntervalMs +
 68                           config::kAdsbMinRefetchMs;

src/core/settings.cpp
285    KV::putU8(kNsRadar, kKeySiteIdx, s_site_index);
286    applyActiveSiteCoords();      // <-- the portal path does not
```
- **Confidence:** Confirmed.
- **Suggested fix:** Move the invalidation to where the centre actually changes rather than to the gesture handler — a `void (*on_center_changed)()` hook set in `main.cpp` and fired from `applyActiveSiteCoords()`/`saveLocationFromStrings()` keeps `core::settings` free of `core/adsb.h`. No memory or FPU impact.
- **Is it pinned by an existing test?** No.

---

### BUG-08 — `skip(length + 4)` overflows, and PNG chunk length is never validated  [Low]

- **Location:** `src/platform/png_decode.cpp:179`
- **Claim:** `skip(length + 4)` computes a `uint32_t` sum that wraps for `length >= 0xFFFFFFFC`, and no code path checks the PNG spec's 2^31−1 chunk-length limit.
- **Failure scenario:** A corrupt or hostile body presents an ancillary chunk with `length = 0xFFFFFFFF`.
  1. `readHeader` falls through to line 179. `length + 4` wraps to `3`.
  2. `skip(3)` consumes three bytes and returns `true`; the reader is now three bytes into what it believes is the next chunk header.
  3. The loop reads a misaligned `length`/`type` pair out of the middle of the corrupt chunk's data, then either mis-parses more headers or drains the body; either way it returns `false`.
  I traced this to confirm it is **not** memory-unsafe: every read goes through `readExactly`/`skip`, both bounded by the body, and neither `width_` nor `chunk_remaining_` sizes a buffer. The consequence is a mis-diagnosed failure, not an out-of-bounds write.
- **Evidence:**
```
src/platform/png_decode.cpp
174        if (memcmp(type, "IEND", 4) == 0) {
175          core::platform::logf("png: no image data\n");
176          return false;
177        }
178
179        if (!skip(length + 4)) {  // ancillary chunk and its CRC
180          return false;
181        }
```
- **Confidence:** Confirmed for the overflow; the "clean failure" downstream claim is reasoned rather than fuzzed, so treat the *impact* as Plausible.
- **Suggested fix:** Reject `length > 0x7FFFFFFF` right after `readUint32` — kills both the overflow and the spec violation in one line. Zero memory cost, no float work.
- **Is it pinned by an existing test?** No. `test_png.cpp` covers ancillary chunks (`test_ancillary_chunks_are_skipped`, `:321`) and truncation, but no fixture carries an out-of-range chunk length.

---

### BUG-09 — PNG height is never bounded, so `PixelFn` can be handed `y > 255`  [Low]

- **Location:** `src/platform/png_decode.cpp:150-151`
- **Claim:** IHDR validation bounds `width_` by `kMaxWidth` but applies no upper bound to `height_`, so a corrupt header can drive `emitRow`'s `y` arbitrarily high and make the decoder chew the entire body before reporting truncation.
- **Failure scenario:** A tile arrives whose IHDR height is corrupted to `0x00010000` (65536).
  1. Lines 150-151 accept it (`bit_depth == 8`, `color_type == 2`, `interlace == 0`, `width_ == 256 <= kMaxWidth`, `height_ > 0`).
  2. `run()` loops while `rows_done_ < 65536`. The real stream has 256 rows, so the bit reader hits end of body, `failed_` is set, and line 297 reports `"truncated after 256 of 65536 rows"` and returns `false`. Correct outcome — but only because the stream ran out.
  3. The dangerous variant is a stream that *does* inflate past row 255: `emit()`'s guard at line 557 is `rows_done_ >= height_`, which with `height_ == 65536` permits rows 256..65535. `emitRow` then calls `on_pixel(ctx, x, y >= 256, …)`. `terrain.h:57-59` calls these "tile-local coordinates" and `png_decode.h:63` promises "up to 256 px wide" without mentioning height, so a sink is entitled to index a 256-entry table with `y`.
  4. Today's only sink is safe: `core::terrain::onPixel` re-checks `y >= kTilePx` at `terrain.cpp:188-190`. So this is a latent contract hole, not a live overflow.
- **Evidence:**
```
src/platform/png_decode.cpp
147          const int bit_depth = ihdr[8];
148          const int color_type = ihdr[9];
149          const int interlace = ihdr[12];
150          if (bit_depth != 8 || color_type != 2 || interlace != 0 ||
151              width_ <= 0 || height_ <= 0 || width_ > kMaxWidth) {

src/core/terrain.cpp
188    if (s_tile_grid == nullptr || x >= static_cast<uint32_t>(kTilePx) ||
189        y >= static_cast<uint32_t>(kTilePx)) {
190      return;
191    }
```
  Note the asymmetry: `width_ > kMaxWidth` is rejected because the row buffers are sized by it; height simply had no buffer to protect, so it was left unbounded.
- **Confidence:** Confirmed that height is unbounded. **Plausible** on impact, because the single current sink guards itself — the bug is that the decoder relies on that.
- **Suggested fix:** Add `|| height_ > kMaxWidth` (or a separate `kMaxHeight = 256`) to line 150-151 and say so in `png_decode.h`. One comparison at header-parse time.
- **Is it pinned by an existing test?** Partly — `test_no_callback_lands_outside_the_image` (`:366`) asserts the sink never sees out-of-range coordinates and `test_a_stream_that_runs_past_the_last_row_is_rejected` (`:451`) pins the `emit()` guard. Neither feeds an oversized *declared* height.

---

### BUG-10 — The `s_in_range` memo caches only hits, and the range test takes a square root  [Low]

- **Location:** `src/ui/runway_overlay.cpp:274-285`, helper at `:73-80`
- **Claim:** `s_in_range[ap_idx]` is only ever set to `true`, so an out-of-range airport is re-projected once per runway instead of once per airport, and each re-projection calls `sqrtf` where a squared comparison would do — on a core where every float op is a library call.
- **Failure scenario:** This is a performance defect, so the "failure" is frame time. Every `renderFrame()` walks all 1706 runways.
  1. For a runway whose airport is out of range, line 275 tests `!s_in_range[ap_idx]` → `true` (still `false` from the reset at 268-269), so the block runs again.
  2. `offsetKmFromCenter` does two float subtractions, four multiplies, an add and a `sqrtf`.
  3. Line 282 sets `s_in_range[ap_idx] = false` again, so the next runway of the same airport repeats it. The memo only short-circuits the in-range minority.
  4. Net: roughly 1700 `sqrtf` calls plus ~10,000 float multiply/add library calls per composed frame, against ~1166 with a working memo and zero `sqrtf` with a squared compare.
  Small next to the 57,600-pixel terrain upsample that `terrain_overlay.cpp:22-32` documents at 209 ms, but the same class of defect on the same hot path.
- **Evidence:**
```
src/ui/runway_overlay.cpp
267    for (size_t i = 0; i < data::large_airports::kAirportCount; ++i) {
268      s_in_range[i] = false;
269      s_label_pending[i] = false;
270    }
271
272    for (size_t i = 0; i < data::large_airports::kRunwayCount; ++i) {
273      const auto& rw = data::large_airports::kRunways[i];
274      const uint16_t ap_idx = rw.airport_idx;
275      if (!s_in_range[ap_idx]) {          // <-- true for every runway of an out-of-range airport
276        const auto& ap = data::large_airports::kAirports[ap_idx];
...
280        offsetKmFromCenter(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &dx_km, &dy_km,
281                           &dist_km);      // <-- contains sqrtf
282        s_in_range[ap_idx] = (dist_km <= radius_km);
283      }

src/ui/runway_overlay.cpp
 79    *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
```
- **Confidence:** Confirmed for the logic (the memo genuinely never short-circuits a miss). The millisecond cost is Plausible — not profiled on hardware.
- **Suggested fix:** Make the memo tri-state (`0 = untested, 1 = in, 2 = out`) — the two existing `bool[1166]` arrays already cost 2332 bytes of static RAM, so replacing one with a `uint8_t[1166]` is free — and drop the `sqrtf` by comparing `dx*dx + dy*dy <= radius_km*radius_km`. Strictly reduces both RAM traffic and float work, so it cannot regress either constraint.
- **Is it pinned by an existing test?** No. `runway_overlay.cpp` has no host test (it needs LovyanGFX, which the test envs deliberately exclude).

---

### BUG-11 — The documented 250 ms inter-tile pause never happens  [Low]

- **Location:** `src/core/terrain.cpp:426-430` (gate) and `:437` (stamp); documented intent at `include/config.h:82-87`
- **Claim:** `s_prog.last_request_ms` is stamped *before* the blocking `HttpClient::get`, so the interval is measured from request start; once a request takes longer than `kTerrainTileIntervalMs` the gate is already satisfied when the next call arrives and the next tile goes out immediately.
- **Failure scenario:** A tile request over TLS takes roughly 700–1500 ms (`config.h:83` says "the whole grid lands in about a second" for 1–4 tiles, consistent with that).
  1. Call N stamps `last_request_ms = T` at line 437, then blocks until `T + 1200`.
  2. `maybeFetchTerrain` returns; `main.cpp` sleeps 10 ms; `loop()` comes round.
  3. Call N+1 evaluates `nowMs() - last_request_ms == 1210`, not `< 250`, so the gate at 426-430 does not fire and the second tile goes out at once.
  4. The intended 250 ms breather is never taken on real hardware. Control *is* still handed back to `loop()` for one iteration per tile, so the button and portal are not starved — but they get ~10 ms per tile instead of ~250 ms, i.e. one `s_wm.process()` call rather than ~25.
- **Evidence:**
```
src/core/terrain.cpp
425    // Hand control back to the main loop between tiles; see the interval's note.
426    if ((s_prog.next_tile > 0 || s_prog.failures > 0) &&
427        platform::nowMs() - s_prog.last_request_ms <
428            config::kTerrainTileIntervalMs) {
429      return false;
430    }
...
436    beginTile(&g, tile);
437    s_prog.last_request_ms = platform::nowMs();
438    const bool ok = platform::HttpClient::get(
439        url, decodeBody, config::kTerrainRequestTimeoutMs, s_poll_fn);
```
- **Confidence:** Confirmed for the ordering. That real requests exceed 250 ms is Plausible-to-obvious (TLS handshake + a 60–150 KB body per `config.h:76`) but not measured.
- **Suggested fix:** Move the stamp to immediately below line 439. Note this changes `test_requests_are_spaced_by_the_tile_interval`, whose fake transport returns in zero fake milliseconds — under the fix the test still passes because the stamp lands at the same fake instant. No memory or FPU impact.
- **Is it pinned by an existing test?** The gate is pinned (`test/test_terrain_fetch/test_terrain_fetch.cpp:319`), but only with an instantaneous transport, which is exactly why the ordering bug is invisible to it.

---

### BUG-12 — `out[0] = '\0'` runs before the `out_len == 0` guard  [Low]

- **Location:** `src/core/adsb.cpp:69-72` and `:84-88`
- **Claim:** Both functions write `out[0]` and only then test whether `out_len` is zero, so a zero-length buffer would be written out of bounds.
- **Failure scenario:** Called as `copyJsonStringTrimmed(plane, "flight", buf, 0)`, the first statement writes `buf[0]`, one byte past a zero-length buffer, before line 71 can return. **This is unreachable from today's callers** — `fillTagFields` passes `sizeof(ac->callsign)` (9), `sizeof(ac->type)` (5) and `sizeof(ac->alt)` (12), all non-zero compile-time constants — so I report it as an inverted guard rather than a live overflow. It matters because the guard's presence signals that zero *is* considered a legal input.
- **Evidence:**
```
src/core/adsb.cpp
 68  void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
 69                             size_t out_len) {
 70    out[0] = '\0';                                   // <-- before the guard
 71    if (out_len == 0 || !obj[key].is<const char*>()) {
 72      return;
 73    }
...
 83  void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
 84    out[0] = '\0';                                   // <-- same
 85    if (out_len == 0) {
 86      return;
 87    }
```
- **Confidence:** Confirmed for the ordering; unreachable from current callers (all three call sites verified at `adsb.cpp:106-111`).
- **Suggested fix:** Move the `out_len == 0` test above the store in both functions, or drop the guard and document `out_len >= 1` as a precondition.
- **Is it pinned by an existing test?** No, and it cannot be through the public API — `parseResponse` never lets a caller choose the buffer size.

---

### BUG-13 — Turning terrain off mid-download wedges the download-active flags permanently  [Low]

- **Location:** `src/main.cpp:101-123`, `src/core/terrain.cpp:223` (`endDownload`), `:239` (`clear`, no production caller)
- **Claim:** `maybeFetchTerrain` returns before computing the download edge, so if the terrain layer is disabled between two tiles, `s_prog.active` and `g_terrain_download_active` are both left `true` with no path that ever clears them.
- **Failure scenario:**
  1. A four-tile download is in flight; tile 1 decoded, `s_prog.active == true`, `g_terrain_download_active == true`.
  2. The user unticks "Show terrain" in the portal. The save callback runs `settings::saveTerrainFromPortal("")` → `s_show_terrain = false`. On the device this callback can even run from inside `HttpClient::get`'s poll hook, i.e. mid-download.
  3. Every subsequent `maybeFetchTerrain()` returns at line 102-104. `g_terrain_download_active` stays `true`; `endDownload()` is never called; `core::terrain::downloadActive()` returns `true` forever.
  4. `core::terrain::clear()` would fix it but has no caller in `src/` (grep: only test `setUp`), so the state persists until reboot.
  Observable damage today is nil, because `downloadActive()` has exactly one consumer (`main.cpp:113`) and re-enabling terrain resumes correctly via `progressMatches`. Reported because it is a public API returning a permanently wrong answer, which the next consumer of that flag will trust.
- **Evidence:**
```
src/main.cpp
101  void maybeFetchTerrain() {
102    if (!g_radar_visible || !wifiIsConnected() || !ui::radar::showTerrain()) {
103      return;                       // <-- edge never computed, flags never cleared
104    }
...
119    if (ready || (g_terrain_download_active && !active)) {
120      ui::radarDisplayDraw();
121    }
122    g_terrain_download_active = active;
123  }

src/core/terrain.cpp
223  void endDownload() { s_prog.active = false; }
```
- **Confidence:** Confirmed for the stuck flags. **Plausible** that it ever matters, for the reason above.
- **Suggested fix:** Have the early return abandon rather than freeze — call `core::terrain::clear()` (which already does `endDownload()` plus invalidates the grid) on the transition into any of those three false conditions, and set `g_terrain_download_active = false`. Because the sprite is fully repainted by `fillScreen` on the next compose, no extra repaint is needed, so this does not weaken the memory invariant. No FPU impact.
- **Is it pinned by an existing test?** No. `main.cpp` has no host test, and `test_terrain_fetch` calls `ensureGrid` directly.

---

## Cleared — things I checked hard and found correct

Recording these so a later pass does not spend the same time, and because several are exactly where the brief pointed.

- **The compose-during-decode invariant.** Traced every caller of `radarDisplayDraw`/`radarDisplayRefreshAircraft` (6 sites, all in `main.cpp`) and every path reachable from `pollWifiAndTaps` on both destinations. No path composes into `s_frame` while `platform_png::decode` is on the stack. The mechanism is that the poll hook calls `gesture::tapPress` but not `gesture::tapPoll`; if that ever changes, `onRangeTap`/`onSiteTap` become reachable from inside a tile decode and this becomes Critical.
- **`Work` fits the scratch.** `2 * sizeof(Huffman)` (1216) + two 768-byte rows + the 32768-byte window = 35,520 ≤ `kScratchBytes` 35,840; the `static_assert` at `png_decode.cpp:53` guards it. `radarDisplayFrameScratch` correctly rejects `need_bytes > 115200`.
- **Huffman table construction cannot index out of bounds.** `buildTable`'s `offsets` sum is bounded by `count`, and in `decodeSymbol` the invariant `code_len >= first_len` holds inductively (the continue condition is `code >= first + count`, and both sides double), so `code - first` is never negative even for an over-subscribed table. `index + (code - first)` is strictly below the total symbol count, hence below 288.
- **LZ77 distance handling.** `kDistBase[29] + (2^13 − 1) == 32768 == kWindowSize` exactly, and `(written_ − 32768) & kWindowMask == written_ & kWindowMask` is precisely the slot holding the byte 32768 back. `distance > written_` correctly rejects reads of never-written window bytes.
- **Adler-32 run length.** `255·5552·5553/2 + 5553·65520 = 4,294,690,200 ≤ 2^32−1`, so `kAdlerRunMax = 5552` cannot overflow either accumulator.
- **`beginTile` reverse maps.** `s_col_first[local_x]`/`s_row_first[local_y]` are only indexed after the `[0, kTilePx)` test, and both `s_col_px` and `s_row_py` are monotone non-decreasing (including after the antimeridian modulo and the Mercator clamp), so the "equal entries are contiguous" assumption the `onPixel` walk depends on genuinely holds.
- **`s_prog.filled` cannot overcount past `kGridPoints`.** Chased the duplicate-tile hypothesis: duplicates need `|tx1 − tx0| >= span`, which requires zoom 0, and `zoomForView` cannot return 0 for any `half_span_km` the range presets produce (zoom 1 always satisfies both span tests below ~10,000 km). Each grid sample lands in exactly one fetched tile and `filled == kGridPoints` exactly.
- **`persistSitesString`'s buffer.** `char buf[30]` with six 4-character idents and five commas uses indices 0..29 inclusive — exact fit, no overflow. Zero margin, but correct.
- **`terrain_overlay.cpp` fixed-point bounds.** `s_cell[i] <= kGrid − 2` after the clamp at `:54-57`, so `row_elev[c + 1]` and `elev_m[(r + 1) * kGrid + kGrid − 1] == elev_m[1680]` are both the last valid element. `i * 40 * 256 / 239` peaks at 10,240 — no overflow.
- **`s_aircraft` bounds.** The `n >= kMaxAircraft` break at `adsb.cpp:243` fires after the increment, so the highest index written is `kMaxAircraft − 1`.
- **`segmentIntersectsDisc` integer overflow** (`runway_overlay.cpp:143-157`): worked the algebra — `disc = 4d²(r² − f²sin²θ)`, maximal at `sinθ = 0` giving `4·14161·11449 ≈ 6.5e8`, and the intermediate `b*b ≤ 1.6e9` — all within int32 for the current `kRunwayLengthScale = 5.0`, `kGridOuterRadius = 107` and real runway lengths. Tight but not a bug.
- **Repeated `loadFont`.** `LGFXBase::load_font` calls `unloadFont()` first (`LovyanGFX/src/lgfx/v1/LGFXBase.cpp:2584`), so `displayFontEnsureLoaded` cannot leak the glyph table on the bitmap-fallback path.
- **`urlDecode`'s escape bound** (`portal_server.cpp:147`): `i + 2 < in.size()` is the correct test for `in[i + 2]` being valid.
- **`ui::radar::rangeIndex()` vs `core::settings::rangeIndex()`** (`terrain_overlay.cpp:122` vs `main.cpp:107`) are the same function — `radar_range.h:34` is a pure forwarder — so the terrain grid lookup key matches the fetch key.
- **The high-latitude Mercator clamp** (`terrain.cpp:295-305`) does smear grid rows for a view above ~85°, but the comment shows that was chosen over dropping them, and no entry in the airport table reaches it. Not reported.
- **`tap_gesture.cpp`, `airport_find.cpp`, `kv_json_file.cpp`, `font_blob_*.cpp`, `main.cpp`'s five globals** — no findings. All `main.cpp`/`terrain.cpp`/`tap_gesture.cpp` timers use the rollover-safe `now − then` form (including the `nowMs() − kAdsbFetchIntervalMs + kAdsbMinRefetchMs` nudge at `main.cpp:67`, which is correct under wrapping); the binary search bounds are correct; `normalizeIcao` correctly requires exactly 4 alphabetic characters. All statics are zero-initialised and no uninitialised member is read.
