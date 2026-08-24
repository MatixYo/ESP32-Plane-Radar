# findings-06 — Test suite and refactor safety net

Read-only audit. **MEASURED** = produced by a command run in this session. **INFERRED** = reasoning over source.

## 0. Verdict

**About one third of the hand-written firmware is behaviourally pinned. Two thirds is protected by nothing but the compiler.**

The tests that exist are unusually good — `test_terrain_fetch` and `test_png` are genuinely strong characterisation suites. But coverage is *deep and narrow*: `core::terrain` and `platform/png_decode.cpp` are nailed down almost completely, while **`src/ui/` (1437 LOC) and `src/platform/` minus the PNG decoder (2408 LOC) have exactly zero behavioural assertions between them** — no test environment even compiles most of those files.

| Refactor move | Protected today? |
|---|---|
| (a) extract pure computation into free functions | **Partly.** Good for `core::terrain`, `core::geo`, `core::adsb`, `platform_png`. **Zero** for the projection/label/tag math in `radar_display.cpp`, `runway_overlay.cpp`, `portal_server.cpp`, `status_screens.cpp`. |
| (b) centralise the 5 `main.cpp` globals + module `s_*` into `AppState` | **No.** `main.cpp` is compiled by no test env. |
| (c) rename NVS keys with migration | **No.** `settings::init()` — the only reader of every persisted key — is never called by any test. |
| (d) hot-path float → fixed point | **Dangerously no.** Existing geo assertions are tolerance-based or directional. A ±1–2 px shift in every aircraft position passes all 112 tests. |
| (e) restructure `radar_display.cpp` | **No.** Only the compiler protects it. |

The most damaging outcome would be a refactor agent reading "112 tests, all green" as "the behaviour is pinned". P0 must be a **substantial** phase.

---

## 1. Measured baseline

### 1.1 `make test` — the claim is exactly right

```
$ make test                                       # warm build cache
72 test cases: 72 succeeded in 00:00:03.030       native_test
14 test cases: 14 succeeded in 00:00:00.366       native_test_fetch
26 test cases: 26 succeeded in 00:00:00.325       native_test_png
make test  2.04s user 1.13s system 71% cpu 4.471 total
```

**MEASURED.** 72 / 14 / 26 = **112 offline cases, 112 passed, 0 failed, 0 ignored.** Claimed counts correct to the case. Warm wall **4.47 s**.

| Suite | Cases | Duration |
|---|---:|---|
| `test_adsb` | 10 | 0.591 s |
| `test_geo` | 15 | 0.587 s |
| `test_settings` | 17 | 0.607 s |
| `test_gesture` | 3 | 0.650 s |
| `test_terrain` | 27 | 0.595 s |
| **`native_test`** | **72** | 3.030 s |
| `test_terrain_fetch` | 14 | 0.366 s |
| `test_png` | 26 | 0.325 s |

Cross-checked: `grep -cE '^\s*RUN_TEST\('` gives 10/15/17/3/27 = 72, 14, 26; each file's `void test_*` count equals its `RUN_TEST` count, so no test is defined and silently never run.

### 1.2 Cold build — **MEASURED**

After `rm -rf .pio/build/native_test{,_fetch,_png}`: 4.515 + 1.183 + 0.935 s of suite time, **7.40 s wall**. Good news: the net is cheap enough to run after every edit. No phase should close without it.

### 1.3 `make test-live` — I ran it; it works

```
test_live_tile_url_shape / test_live_graz_grid_is_plausible / test_live_alpine_grid_is_high  [PASSED]
3 test cases: 3 succeeded in 00:00:07.228
make test-live  0.58s user 0.22s system 10% cpu 7.605 total
```
**MEASURED.** 3/3, 7.23 s, real AWS tiles + real `png_decode.cpp`. Grand total across four envs: **115 cases, 115 pass.**

### 1.4 `make test-build` — **MEASURED**
`make test-build SUITE=test_geo` works as documented: compiles, reports `0 test cases`, 0.75 s.

### 1.5 A baseline caveat the plan must absorb

`git status --porcelain` (**MEASURED**): `.gitignore`, `README.md`, `scripts/gen_png_fixtures.py`, `src/platform/png_decode.cpp`, `test/test_png/png_fixtures.h`, `test/test_png/test_png.cpp` modified; `scripts/__pycache__/*.pyc` deleted.

**The 26-case `test_png` baseline exists only in the working tree:**

| | Fixtures | `test_png` cases |
|---|---:|---:|
| HEAD (`3a17221`) | 16 | **21** |
| working tree | 23 | **26** |

The five uncommitted cases are exactly the Adler-32 / trailer / overrun family (`kRejectWrongAdler`, `kRejectSilentCorruption`, `kRejectMissingAdler`, `kRejectExtraScanline`, `kEmptyFinalBlock`) — the most valuable in the suite, being the only checks on the *data* rather than the *structure*.

**Action:** master-plan P0 step 1 is not cosmetic. Until it happens, any agent starting from a clean HEAD checkout, or any `git checkout`/worktree parallelism, silently loses 5 of 26 PNG cases and gets a `png_decode.cpp` the remaining fixtures may not match.

---

## 2. Coverage map

### 2.1 What each env compiles (from `platformio.ini`)

| Env | Sources built |
|---|---|
| `native_test` | `core/` + `native/platform_native.cpp` + `native/kv_json_file.cpp` + `native/http_curl.cpp` |
| `native_test_fetch` | `core/` + `native/kv_json_file.cpp` (test file supplies `nowMs`, `logf`, `HttpClient::get`) |
| `native_test_png` | `platform/png_decode.cpp` **only** |
| `native_test_live` | `native_test` + `platform/png_decode.cpp` |

**Nothing under `src/ui/` is compiled by any test env. Neither is `src/main.cpp`. Neither is anything under `src/platform/device/`.**

### 2.2 LOC accounting (**MEASURED** `wc -l`, excluding generated `large_airports_data.cpp` 2884)

| Group | LOC | Compiled by a test env | Has ≥1 direct assertion |
|---|---:|---|---|
| `src/main.cpp` | 195 | **no** | **no** |
| `src/core/` | 1539 | yes | 1378 of 1539 |
| `src/platform/png_decode.cpp` | 732 | yes | yes |
| `src/platform/device/` | 866 | **no** | **no** |
| `src/platform/native/` | 1542 | 415 of 1542 | **0 of 1542** |
| `src/ui/` | 1437 | **no** | **no** |
| **Total** | **6311** | **2686 (42.6 %)** | **2110 (33.4 %)** |

**Requested quantification:**

- **`src/ui/` rendering: 0 of 1437 LOC (0 %) compiled by any test env; 0 % asserted.**
- **`src/platform/` WiFi/HTTP/portal/NVS: of 2408 LOC (all of `platform/` except `png_decode.cpp`), 415 LOC (17 %) are linked into `native_test` — `platform_native.cpp` 58, `kv_json_file.cpp` 215, `http_curl.cpp` 142 — and 0 LOC (0 %) has a single assertion.** The other 1993 LOC (83 %) are compiled by no test env: `wifi_setup_device.cpp` 501, `portal_server.cpp` 575 (+47 hdr), `wifi_setup_native.cpp` 202, `http_arduino.cpp` 170, `kv_nvs.cpp` 105, `button_sdl.cpp` 101, `main_native.cpp` 101, `font_blob_file.cpp` 56, `platform_device.cpp` 49, `font_blob_embedded.cpp` 26, `display_native.cpp` 15.

One mitigating fact (**INFERRED** from `platformio.ini` + CI): `env:native` builds `+<*> -<platform/device/>` and `env:supermini` builds `+<*> -<platform/native/>`, and CI runs both, so **compile coverage of `src/` is 100 %**. **Behavioural coverage is 33 %.** Keep the two apart — the refactor will produce plenty of code that compiles and is wrong.

### 2.3 Per-module table

| Module | File | LOC | Test file(s) | What IS pinned | What is NOT pinned | Risk |
|---|---|---:|---|---|---|---|
| `core::geo` | `core/geo.cpp` | 86 | `test_geo` (15) | Sign conventions (north = −y, east = +x); centre → (120,120) exactly; `innerRingMaxKm` inset shrink; `isInsideOuterRingKm` inclusive at edge; `rimPointForDistantTarget` declines inside-ring/centre, cardinal bearings ±1 px, rim-independence of distance; `clipPointToOuterRing` inside/outside/degenerate | **Exact pixel values for any non-centre point.** `test_north_moves_up_the_screen` asserts only `p.y < 120`; `test_outer_km_lands_on_the_outer_ring` allows `INT_WITHIN(1)`. `distSqFromCenter` overflow if types change. **The 20-step `t -= 0.05f` clip search geometry** — a closed-form rewrite lands on different pixels and passes. `lroundf` half-way behaviour | **High** (a, d) |
| `core::gesture` | `core/tap_gesture.cpp` | 49 | `test_gesture` (3) | Single tap fires at exactly `>= kDoubleTapWindowMs`, not 1 ms earlier; double inside window; two spaced taps = two singles; ready-tap consumed once | **`unsigned long` wraparound at 49.7 days** — `now_ms - s_first_tap_ms` is the whole mechanism, untested near `ULONG_MAX`. Triple tap. Press with no intervening poll | Low |
| `core::airport` | `core/airport_find.cpp` | 63 | `test_settings` (2) | Known ICAO resolves + returns `ident`; unknown and 3-char rejected | **Case normalisation** (`normalizeIcao` upper-cases; `"lowg"` never tested). **Non-alpha rejection.** **The binary search** — that `kAirports` is sorted, and first/last-entry boundaries | Med (a) |
| `core::adsb` | `core/adsb.cpp` | 297 | `test_adsb` (10) | `ElementScanner` framing: `"ac"` key match, `"ac":null`, `[]`, missing array = error, truncated = error, cap at `kMaxAircraft`, short-reply-replaces-store; `true_heading > track` precedence; callsign trailing-space trim; `alt_baro` → `"34000 ft"`; ground skip; `buildUrl` km→NM to 1 dp | **Fallback chains past the first entry** — `mag_heading`, `dir` (`adsb.cpp:34-51`), `tas`/`ias` (`:53-59`), `alt_geom` (`:99-101`) all untested. **`"ground"` → `"GND"` tag** (unreachable while `kAdsbShowGroundAircraft == false`). **`fetchUpdate`** end to end. **`ElementScanner::readBytes` pushback** (`:138-150`) — `MemoryBodyReader` never short-reads | Med (a) |
| `core::settings` | `core/settings.cpp` | 384 | `test_settings` (17) | `parseCoord` accept/reject incl. trailing garbage; `validLatLon` bounds inclusive; WiFiManager `T`/`t`/`F`/`f`/`on` quirk; `formatRing3Label` exact strings, 4 presets × both units; `outer_km == ring3_km × 4/3`; `rangeNext` wraps; `unitsReset` leaves range alone; `saveSites` resolves/drops unknowns/cycles/moves centre | **`init()` — never called by any test.** So: reading `lat`/`lon` from `kNsLocation`, the `validLatLon` guard on stored values, `rangeIdx` clamp, the three bool defaults, `loadSitesFromStorage` (comma/space/tab split, over-long idents, unknown-ident drop, `siteIdx` clamp, re-persist), `applyActiveSiteCoords`. **`saveLocationFromStrings`' "only apply when `s_site_count == 0`" rule** (`:184-187`). **`clearLocation`'s key set.** **`siteNext`'s `< 2` guard.** **`persistSitesString` buffer** — `kMaxSites*5 = 30` for 6×4 idents + 5 commas + NUL = exactly 30, no slack | **High** (a, c) |
| `core::terrain` | `core/terrain.cpp` | 499 | `test_terrain` (27), `test_terrain_fetch` (14), `test_terrain_live` (3) | Best-covered module. `pointLatLon` centre/corners/linearity; `terrariumElevation` sea level, whole metres, sub-metre blue, `INT16_MAX` saturation; `latLonToTilePixel` vs **off-line** slippy-map literals + Mercator clamp; `zoomForView` maximality, standard formula (8/7/6/5), monotonicity, poles; `tilesForView` coverage over 5 stress centres × 4 presets, 1/2/4-tile, `max_out`/`nullptr` truncation; `buildTileUrl` z/x/y + truncation; `bandForElevation` boundaries. State machine: one request per tile in order, spacing gate, in-place retry preserving decoded tiles, abandon after 3 failures + retry gate + gate-is-per-preset, view change restarts, single-slot cache + eviction, exactly one decode per tile, no decoder = no download, resampler correct across the antimeridian sample-by-sample | **`downloadActive()` — asserted nowhere in the repo** (`grep -rn downloadActive test/` → zero hits). This is the borrowed-scratch guard. **The partial-fill discard path** (`:464-473`) — the fake always fills, so the branch never executes. **`tile_count == 0`** (`:414-421`). **`progressMatches`' `kCenterEpsilonDeg` boundary** (1e-7) and exact `half_span_km ==` float compare. **`s_poll_fn` invocation** — the fake calls `poll()` but nothing asserts it. **`bandForElevation` against the real `radar::kTerrainBandMinM`** — `test_terrain.cpp:519` uses a local copy, so shipped thresholds are unpinned | Med — but the specific gaps are high-value |
| `core::portal` | `core/portal_params.cpp` | 161 | **none** | nothing | **Entire module — compiled into `native_test` and never called.** Unpinned: the 11-field table; `isSiteField`'s `site_1..site_6` parse; `currentValue`'s `%.6f`; `htmlAttrs`' `" checked"`; `applyValue`'s dispatch — note the asymmetry: sites/lat/lon are *staged* in `s_pending_*`, the three checkboxes are *committed immediately*; `applyValueById` unknown-id false; **`commit()`'s ordering — `saveSites` before `saveLocationFromStrings`, which is what makes the "lat/lon only applies when no sites" rule work**; the `s_pending_*` clear | **High** (a, b) — 161 LOC of pure, trivially testable logic, zero tests |
| `platform_png` | `platform/png_decode.cpp` | 732 | `test_png` (26), `test_terrain_live` (3) | Exemplary. All 5 row filters + per-row switching; all 3 DEFLATE block types; IDAT split across chunks and mid-scanline; ancillary chunks; empty final block; 256×256 tile via FNV-1a/64 over RGB in callback order; scratch canary (`kScratchBytes + 256`, guard byte checked); null scratch = clean failure, zero callbacks; rejects greyscale, interlaced, truncated, corrupt-deflate, bad signature, wrong Adler, silent corruption, missing trailer, over-long stream — with `bounds_ok` proving nothing lands past declared height; every decoding fixture asserted to decode *silently* | **Zero-length IDAT** — the brief lists it; I found **no such fixture symbol and no such case** in the working-tree header. Verify before relying on it. 16-bit depth, palette, alpha (only greyscale is fixture-tested). Width > 256 rejection. `readBytes` short-read mid-chunk-header | Low |
| `ui::radar` (range) | `include/ui/radar_range.h` | 81 hdr | **none** | nothing | **`fetchRadiusKm()` and `terrainHalfSpanKm()`** — both feed the terrain download and ADS-B radius. `test_terrain.cpp:45` *reproduces* `120.0f/107.0f` as a literal on purpose, so the two can drift apart and the terrain zoom tests keep passing against the stale copy | **High** (a) |
| `ui::radar` (draw) | `ui/radar_display.cpp` | 696 | **none** | nothing | **Entire file.** Unpinned pure logic that needs no LovyanGFX: `speedLineLengthPx` (the `1.852 × 60/3600 × 107 / 13.3 × 0.3` chain + `kAircraftSpeedLineMinPx` floor); `noseTip`; `drawHeadingTriangle`'s three vertices; two duplicated far-first insertion sorts (**not stable for equal `dist_sq`**); tag side-selection (`x < kCenterX` → right) + `std::min/max` clamps; the `draw_count > kTagCompactAboveCount` (=4) compact rule and the deliberate exclusion of rim dots from the count; `drawRings`' `(outer_radius * i) / kRingCount` integer division; `scaleLabelAnchorX`/`siteLabelAnchorX`; `initPalette`'s `kDisplayRgbOrder` R/B swap **applied to the aircraft colour only**. Plus LGFX-coupled: `findVlwSizeForHeight`'s 16-step bisection, `pickGfxFontClosest`, `s_scale_label_max_w` over all presets × units, `radarDisplayFrameScratch`'s `need_bytes > kFrameBytes` guard | **High** (e, a, b, d) |
| `ui::runway` | `ui/runway_overlay.cpp` | 307 | **none** | nothing | **Entire file — and it contains a second, independent copy of the projection**: its own `offsetKmFromCenter`, `latLonToScreen`, `distSqFromCenter`, `clipPointToOuterRing`, `kKmPerDeg = 111.0f` (`:17,71-127`) duplicating `core::geo` and **not** the functions `test_geo` exercises. Also: `segmentIntersectsDisc`'s quadratic with `disc = b*b - 4*a*c` in **`int`** (`:149`) — `b*b` can reach ~10⁹, near `INT_MAX`; the `kRunwayLengthScale = 5.0` midpoint stretch; `clipPointOntoOuterRing`; `offsetLabelFromCenter`; the `kMaxAirportLabels = 32` cap | **High** (a) |
| `ui::terrain` | `ui/terrain_overlay.cpp` | 139 | **none** | nothing | **Entire file — and this is the one already-converted fixed-point hot path.** Unpinned: `initPixelToGridMap`'s `i*(kGrid-1)*kFracOne/(kSize-1)` and the `cell > kGrid-2 → cell = kGrid-2, frac = kFracOne` last-pixel case; `bandAtPixel`'s horizontal lerp; `drawScanline`'s vertical row blend and the `int32` no-overflow claim; **the run-coalescing loop** (`run_band` computed at `x=0`, loop starts at `x=1`, final run uses `kSize - run_start`); the "band −1 draws nothing so water stays background" rule | **High** (d, a) — the reference implementation for every future fixed-point conversion, and not one number is checked |
| `ui` (fonts/status) | `display_font.cpp` 49, `status_screens.cpp` 246 | 295 | **none** | nothing | **`fitSsidLine`** (`status_screens.cpp:110-127`) — pure descending-truncation loop gated only on `tft.textWidth()`; note the 3-byte UTF-8 ellipsis and `%.*s` cutting on *bytes*, so a multi-byte SSID can be cut mid-codepoint. **`drawSpinnerDots`' `fade = 255 - i*22`** (`:171`) — no clamp; goes negative above 11 dots. **`statusScreenConnectingTick`'s angle wrap** `>= 270.0f → -= 360.0f` (`:203-205`). `drawTextBlock`'s vertical centring (`:77-95`, gap between not after lines). `displayFontEnsureLoaded`'s per-`gfx` idempotence; `displayFontSetBitmap` also forcing `setTextSize(1)` | Med |
| platform: NVS | `kv_nvs.cpp` 105, `kv_json_file.cpp` 215 | 320 | linked, never asserted | nothing | **The whole `KeyValueStore` contract, on both backends.** `has()` for a JSON-`null` member; `getU8`'s range-check fallback (stored 300 → `def`); `getDouble` accepting an integer; `remove` on a missing namespace; the tmp-file + `rename` atomic write; malformed-JSON → behave-as-empty. **The two backends are never compared against each other**, so they can diverge on exactly the semantics a migration depends on | **High** (c) |
| platform: HTTP | `http_arduino.cpp` 170, `http_curl.cpp` 142 | 312 | `http_curl` linked, never asserted | nothing | `performGetWithPoll`'s **retryable-vs-fatal error classification** (`http_arduino.cpp:39-48`) — pure predicate. `StreamBodyReader::readBytes` (`:76-89`) — pure buffer arithmetic. `fill`'s "connection closed and drained" compound (`:119-121`). **`http_curl.cpp:101-102`'s `std::min(timeout_ms, kMaxTimeoutMs)`** — caps the caller's 10000 ms at 2500 ms; one pure line, behaviourally significant, untested. `curl` partial body never handed on (`:124-129`) | Med |
| platform: WiFi/portal | `wifi_setup_device.cpp` 501, `portal_server.cpp` 575, `wifi_setup_native.cpp` 202, `portal_server.h` 47 | 1325 | **no** | nothing | **`portal_server.cpp` holds the largest block of genuinely pure host-testable logic in the repo, all in an anonymous namespace:** `urlDecode` (`:145-170` — note the conservative `i + 2 < in.size()` means a `%41` at the exact end of the string is **not** decoded), `htmlEscape`, `declaredContentLength` (`:198-217`, with an overflow short-circuit returning `kMaxRequestBytes + 1`), `requestComplete`, `parseFormBody` (`:360-386`), `wasSubmitted`, `appendField` (`char input[256]` silent truncation), `renderForm`, `renderSaved`, request-line parsing (`:456-497` — npos propagation, 400/404/405 dispatch), and **`applySubmission` (`:408-430`) — the absent-checkbox pass, documented at `:397-407` as load-bearing because an untick is otherwise indistinguishable from field-absent.** Device side: `bootButtonPollLongPress` (`:391-413`) is pure arithmetic + boolean state and is **structurally different** from `button_sdl.cpp:65-101`; `tryConnectWithUi`'s 3-attempt ladder (`:297-324`); `wifiSetupConnect`'s six-outcome boot tree (`:443-501`); the `wifiLinkUp` vs `wifiIsConnected` distinction documented in `wifi_setup.h:3-11`. Native `wifiLoop`'s re-entrancy guard `s_in_wifi_loop` can be **left set** if `bootButtonPollLongPress()` calls `exit()` (`:196-198`), and native has **no `statusScreenConnectFailed` path at all** — a structural divergence from the device tree. **`wifi_setup_device.cpp` uses raw `Preferences` (`:130-161`, `:366-371`), bypassing the `KeyValueStore` seam entirely** | **High** (c) |
| shell | `src/main.cpp` | 195 | **no** | nothing | **Entire file.** The WiFi-down grace + reconnect ladder (`:163-181`); `g_radar_visible` transitions and log-once; the ADS-B cadence and `onSiteTap`'s `nowMs() - kAdsbFetchIntervalMs + kAdsbMinRefetchMs` back-date trick (`:67-68`); **`maybeFetchTerrain`'s falling-edge repaint** `ready || (g_terrain_download_active && !active)` (`:119`); tap → range/site dispatch; `setup()`'s wiring order | **High** (b) |

---

## 3. Blind spots ranked

### Rank 1 — The falling-edge terrain repaint and `downloadActive()`

`main.cpp:113-122` is the only thing between the user and a frame composed out of the PNG decoder's DEFLATE window:

```cpp
const bool active = core::terrain::downloadActive();
if (ready || (g_terrain_download_active && !active)) { ui::radarDisplayDraw(); }
g_terrain_download_active = active;
```

**Test that would catch it:** drive `ensureGrid` through a multi-tile download and assert the exact sequence `downloadActive()` returns — `false → true → … → false` — including that it goes false on *abandonment* (3 failures) and on the *partial-fill discard*, not only on success. Then assert at shell level that a repaint happens on, and only on, that edge.

**Exists? No.** `downloadActive()` is referenced by zero test files (**MEASURED**). P3's "encode §3a in the type system" rewrites precisely this with nothing underneath it. Highest-risk gap in the project: the failure mode is corrupted pixels on a device nobody is watching.

### Rank 2 — Fixed-point conversion has no golden values anywhere

| Assertion | File:line | Slack granted |
|---|---|---|
| `INT_WITHIN(1, 120 - 107, p.y)` | `test_geo.cpp:86` | ±1 px |
| `TRUE(p.y < 120)` | `test_geo.cpp:71` | 119 px |
| `INT_WITHIN(1, 120, out.x)` ×6 | `test_geo.cpp:131-144` | ±1 px |
| `FLOAT_WITHIN(0.01f, …)` | `test_geo.cpp:38-53` | 0.01 km ≈ 0.08 px |

Only two assertions in the whole suite pin an exact pixel (centre → 120,120; the clip equalities). **A fixed-point rewrite of `latLonToScreen` that moves every aircraft 1–2 px passes all 112 tests.** `ui/terrain_overlay.cpp` is worse: already fixed point (`kFracBits = 8`), the template every future conversion will copy, and **not one of its numbers is checked**.

### Rank 3 — `settings::init()` is the only reader of the persisted format, and nothing calls it

Complete persisted surface (**MEASURED**):

| Namespace | Key | Type | Written by | Read by |
|---|---|---|---|---|
| `"radar"` (`kNsLocation`) | `lat`, `lon` | double | `saveLocationFromStrings` | `init`, `saveSites` |
| `"planeradar"` (`kNsRadar`) | `rangeIdx` | u8 | `rangeNext` | `init` |
| `"planeradar"` | `useKm`, `showRwys`, `showTerr` | bool | `save*FromPortal` | `init` |
| `"planeradar"` | `sites` | string | `persistSitesString` | `loadSitesFromStorage` |
| `"planeradar"` | `siteIdx` | u8 | `siteNext`, `saveSites` | `loadSitesFromStorage` |
| `"wifi"` | `ssid` | string | `portal_server.cpp:444` | `portal_server.cpp:307`, `wifi_setup_native.cpp:65` |
| `"wifi"` | `portal` | bool | `wifi_setup_native.cpp:70`; **`wifi_setup_device.cpp:134` via raw `Preferences`** | `wifi_setup_native.cpp:76,134`; `wifi_setup_device.cpp:153,370` |

**Migration traps, all MEASURED:**
1. **The constant names are inverted relative to their literals**: `kNsLocation == "radar"`, `kNsRadar == "planeradar"` (`settings.h:63-64`). A rename done by name rather than by literal will cross the two namespaces.
2. **`"wifi"` and `"portal"` are each declared twice**, in files that never link together: `wifi_setup_device.cpp:60-61` vs `wifi_setup_native.cpp:42` + `portal_server.h:20-21`. A rename must touch both.
3. **`"ssid"` exists only in the native store.** The device has no counterpart — credentials live in the driver's `nvs.net80211`, reached only via `esp_wifi_get_config` / `WiFiManager::erase()` and unmigratable.
4. **The device force-portal flag bypasses `KeyValueStore`.** A `KeyValueStore`-level migration shim cannot see it.
5. **NVS caps namespace and key names at 15 chars.** `"planeradar"` (10) has headroom; a verbose rename may not.

`settings::init()` is called by exactly one thing — `ui::radar::rangeInit()` (`radar_range.h:31`) from `setup()`. **No test calls it.** A rename that forgets `showTerr`, or a migration that mis-parses `sites`, or writes a `double` where a `u8` is read, gives a green suite and a device that boots to the wrong airport with terrain off.

### Rank 4 — `radar_display.cpp` has no test and cannot easily get one

`radar_display.cpp:3` includes `<lgfx/v1/lgfx_fonts.hpp>` and `:57` holds `LGFX_Sprite s_frame(&tft)` at namespace scope, so **the whole TU drags LovyanGFX in** — and on macOS, through it, SDL2's `#define main`: the documented trap that forced `native_test_png` and `native_test_live` to link neither.

So "just add a test for `radar_display.cpp`" is not an option. The pure math must move to a LovyanGFX-free TU *first* — which is exactly what move (a) does. **Move (a) must precede move (e), and the tests must be written in the same commit that extracts the functions.** Extracting and testing later is the same as not testing.

### Rank 5 — `AppState` centralisation has no shell-level test to land against

**MEASURED: 44 mutable module-level statics across `src/`, plus 5 file-scope globals in `main.cpp`, and no `*ForTest`/`resetForTests` entry point anywhere** (`grep -i` finds none).

The subtle hazard: several statics are *initialisation latches* — `s_frame_ready`, `s_label_metrics_ready`, `s_tag_label_metrics_ready`, `s_map_ready`, `s_runway_label_ready`, `s_vlw_loaded` — whose "compute once, keep forever" semantics the code depends on. `core/platform.h:40-51` documents that native `reboot()` is `exit(0)` *because* those latches would survive an in-process restart. Move them into a struct that is default-constructed elsewhere and they silently reset, or silently stay set. **No test observes any of them.**

Also **MEASURED**: `wifi_setup_device.cpp:23-28` declares `s_boot_tap_pending`, `s_boot_is_down`, `s_boot_down_ms`, `s_long_press_handled`, `s_boot_interrupt_attached` **outside** any anonymous namespace — external linkage, colliding by name with `button_sdl.cpp:33-36`'s file-private ones. Harmless only because the two never link together. An `AppState` merge across the device/native split must not assume they are distinct symbols.

### Rank 6 — Duplicated logic the tests do not reach

Three duplications a de-duplicating refactor will have to resolve, none testable today:

- **Projection:** `runway_overlay.cpp:17,71-127` re-implements `core::geo`, with a different signature (`Viewport` vs reading `settings::lat()`/`rangeCurrent()` directly). `test_geo` covers only the `core::geo` copy.
- **Boot-button state machine:** `wifi_setup_device.cpp:30-45` (ISR) + `:391-413` (poll) vs `button_sdl.cpp:65-101` (poll only) — *structurally different*: the device clears the long-press latch in the `else` branch and classifies taps in the ISR; native clears it on every not-down poll. Pure arithmetic with two injectable dependencies (`wifiBootButtonPressed`, `nowMs`), and the highest-value untested unit in the platform layer.
- **String constants:** `platform_device.cpp:46` hardcodes `"or 192.168.4.1"` while `config.h:17` already defines `kPortalIp`. `button_sdl.cpp:30`'s `kNativeBootGpio = 9` mirrors `config::kBootPin` with only a comment enforcing it. The `ssid.length() > 0 ? ssid : "network"` fallback appears three times (`wifi_setup_device.cpp:302`, `status_screens.cpp:184`, `wifi_setup_native.cpp:186`).

---

## 4. Characterisation tests to add BEFORE refactoring

These pin **current** behaviour, correct or not, so the refactor is provably behaviour-preserving. Bug fixes are P1's job. Where I know a pinned value is arguably wrong, I say so and recommend pinning it anyway with a `TODO(P1)`.

### 4.1 `test_render_math` — pure draw math, extracted and pinned
**Priority 1. Effort high (6–10 h). Value highest.**

**Target env:** new `native_test_render`, `build_src_filter = -<*> +<core/> +<ui/render_math.cpp>`, and critically **no LovyanGFX in `lib_deps`** — same discipline as `native_test_png`.

**Needs a new seam: yes, and that is the point.** Create `src/ui/render_math.cpp` + header holding *verbatim copies* (not rewrites) of the pure functions now buried in the anonymous namespaces of `radar_display.cpp` and `runway_overlay.cpp`, and have those files delegate. `radar_theme.h` is safe to include — **confirmed: constants only, zero runtime computation** (only three `constexpr` derivations, at `:63`, `:64`, `:69-70`). Lift: `speedLineLengthPx`, `noseTip`, `headingTriangleVertices`, both far-first sorts, `tagAnchor`, `ringRadius`, `scaleLabelAnchorX`, `siteLabelAnchorX`, `segmentIntersectsDisc`, `clipPointOntoOuterRing`, `offsetLabelFromCenter`, `stretchedRunwayEndpoints`.

```cpp
void test_speed_line_length_is_exact(void) {
  // Golden table CAPTURED FROM THE CURRENT BUILD, not hand-derived: the point is
  // to detect change, and a hand-derived value would encode my arithmetic, not
  // the firmware's. Regenerate deliberately, never to "make it pass".
  TEST_ASSERT_EQUAL_INT(kAircraftSpeedLineMinPx, ui::render::speedLineLengthPx(0.0f));
  TEST_ASSERT_EQUAL_INT(/*golden*/ 0, ui::render::speedLineLengthPx(120.0f));
  TEST_ASSERT_EQUAL_INT(/*golden*/ 0, ui::render::speedLineLengthPx(420.5f));
  TEST_ASSERT_EQUAL_INT(kAircraftSpeedLineMinPx, ui::render::speedLineLengthPx(1.0f));
}

void test_far_first_sort_order(void) {
  // dist_sq DESCENDING. Equal keys are the interesting case: the current sort is
  // NOT stable for them. Pin whatever it does today.
  ui::render::AircraftDrawItem items[4] = {{0,0,0,100},{1,0,0,400},{2,0,0,400},{3,0,0,25}};
  ui::render::sortDrawItemsFarFirst(items, 4);
  const size_t expect[] = {1, 2, 0, 3};   // verify against the build
  for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_size_t(expect[i], items[i].index);
}

void test_segment_intersects_disc_no_int_overflow(void) {
  // b*b - 4*a*c is int (runway_overlay.cpp:149). Drive the widest segment the
  // 240 px frame allows and pin the answer, so a type change shows.
  TEST_ASSERT_TRUE (ui::render::segmentIntersectsDisc(0, 0, 239, 239));
  TEST_ASSERT_FALSE(ui::render::segmentIntersectsDisc(0, 0, 0, 20));
}
```

### 4.2 `test_terrain_upsample` — golden values for the existing fixed-point path
**Priority 1. Effort low (2–3 h). Value very high.** Cheapest high-value test in the list. Touches only `int32_t` arithmetic plus `bandForElevation` — **no LovyanGFX, no new fake**; the only seam is making `initPixelToGridMap`/`bandAtPixel`/the row blend reachable.

```cpp
// s_cell / s_frac for kGrid=41, kSize=240, kFracBits=8. Computed independently
// from the formula, then confirmed against the build:
//   i=0 -> cell 0, frac 0      i=1   -> cell  0, frac  42
//   i=60 -> cell 10, frac 10   i=119 -> cell 19, frac 234
//   i=120 -> cell 20, frac 21  i=121 -> cell 20, frac  64
//   i=238 -> cell 39, frac 213 i=239 -> cell 39, frac 256  <- clamped last pixel
void test_pixel_to_grid_map_golden(void) { /* assert all eight, exactly */ }

void test_row_blend_is_exact_at_the_ends(void) {
  // wy == 0 must give the north row byte-for-byte; wy == kFracOne the south row.
}
void test_no_overflow_on_absurd_terrarium_values(void) {
  // terrain_overlay.cpp:77 claims int32 is safe "for any pair of terrarium
  // values". Pin it: INT16_MIN against INT16_MAX, both lerps.
}
void test_run_coalescing_matches_per_pixel_bands(void) {
  // Strongest property available, needs no gfx: the coalesced runs must tile
  // [0,240) exactly and every pixel's run band must equal bandAtPixel(x).
  // Catches the run_start / kSize - run_start off-by-one.
}
void test_shipped_band_floors(void) {
  // radar::kTerrainBandMinM, NOT a local copy (test_terrain.cpp:519 uses one).
  TEST_ASSERT_EQUAL_INT(-1, bandForElevation(0,    radar::kTerrainBandMinM, 7));
  TEST_ASSERT_EQUAL_INT( 0, bandForElevation(1,    radar::kTerrainBandMinM, 7));
  TEST_ASSERT_EQUAL_INT( 6, bandForElevation(9000, radar::kTerrainBandMinM, 7));
}
```

### 4.3 `test_settings_migration` — the NVS rename, proven lossless
**Priority 1. Effort medium (3–5 h), most of it seam. Value very high.** Target env `native_test` (`kv_json_file.cpp` is already linked, so a real `KeyValueStore` exists on the host — no new fake needed).

**Needs a new seam, one of two.** `kv_json_file.cpp:45` resolves the path into a function-local `static const std::string` and `:115` loads the document into a function-local `static JsonDocument`. **Both are one-shot per process** (documented at `:42` and `:113`). A test binary can point at one scratch file, once, and can never force a re-read — so it can simulate "device boots with old keys" exactly once per binary. Not enough.

1. **(Recommended) Make the migration a pure function.**
   ```cpp
   struct StoredSettings { bool has_lat, has_lon; double lat, lon;
                           uint8_t range_idx; bool use_km, show_rwys, show_terr;
                           char sites[31]; uint8_t site_idx; };
   StoredSettings readLegacy(const KeyValueStore&);    // effectful, thin
   StoredSettings migrate(const StoredSettings& old);  // PURE — test this
   void writeCurrent(KeyValueStore&, const StoredSettings&); // effectful, thin
   ```
   The round-trip test then needs no filesystem, no env var and no reset seam, and can enumerate hundreds of legacy states. Also fits D1/D2a better than a KV-driven test.
2. Add `KeyValueStore::reloadForTest()` to both backends. Cheap, but puts a test-only method on a production seam and still leaves *device* NVS semantics unverified.

```cpp
void test_migration_is_lossless_for_every_legacy_key(void) {
  StoredSettings old{}; old.has_lat = old.has_lon = true;
  old.lat = 52.3676; old.lon = 4.9041; old.range_idx = 3; old.use_km = true;
  old.show_rwys = false; old.show_terr = false;
  strcpy(old.sites, "LOWG,LOWW,EHAM"); old.site_idx = 2;
  const StoredSettings now = migrate(old);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, 52.3676, now.lat);   // full double precision
  TEST_ASSERT_EQUAL_UINT8(3, now.range_idx);
  TEST_ASSERT_FALSE(now.show_terr);          // the one most likely to be dropped
  TEST_ASSERT_EQUAL_STRING("LOWG,LOWW,EHAM", now.sites);
  TEST_ASSERT_EQUAL_UINT8(2, now.site_idx);
}
void test_migration_of_a_virgin_device_yields_the_defaults(void) {}
void test_migration_is_idempotent(void) { /* migrate(migrate(x)) == migrate(x) */ }
void test_migration_preserves_a_full_six_site_list(void) {
  // "LOWG,LOWW,EHAM,EDDF,LFPG,EGLL" = 29 chars + NUL = 30 = exactly kMaxSites*5.
  // persistSitesString has no slack; prove the migration doesn't either.
}
void test_migration_does_not_touch_the_wifi_namespace(void) {
  // "wifi"/{ssid,portal} must survive verbatim; losing `ssid` re-opens the portal
  // on every configured device. Note wifi_setup_device.cpp:130-161 writes
  // `portal` through raw Preferences, NOT KeyValueStore -- assert the migration
  // leaves that namespace alone, because it cannot see it.
}
void test_namespace_and_key_names_fit_the_nvs_15_char_limit(void) {
  // Static assertion in test form, so a verbose rename fails on the host.
}
```

Plus one non-pure suite (its own directory, so it gets its own binary and can own the one-shot env var): an `init()`-reads-what-`save*`-wrote test asserting the full `loadSitesFromStorage` parse — comma/space/tab separators, an over-long ident, an unknown ident dropped with the rest kept, `siteIdx` clamped.

**Hazard:** only `test_settings.cpp:211` sets `PLANE_RADAR_SETTINGS`. A new suite that touches `core::settings` and forgets it will read and rewrite the developer's real `~/.plane-radar/settings.json` (**confirmed present, 178 bytes**). Put the `setenv` in every new settings-touching suite's `main()` before the first call.

### 4.4 `test_shell` — the loop tick and the borrowed-scratch edge
**Priority 1 for the terrain edge, 2 otherwise. Effort high (8–12 h). Value high.**

Blocker: `loop()` calls `ui::radarDisplayDraw()`, `wifiIsConnected()`, `bootButtonPollLongPress()`, `displayInit()` — all pulling in LovyanGFX. And `include/platform/wifi_setup.h` is 11 free functions in the global namespace with **no injection points and no reset hooks at all** — that is the structural reason neither WiFi implementation is host-testable. The shell must be extracted as `loopTick(AppState&, const Ports&)` with `Ports` a POD of plain function pointers (no `std::function` — §3c).

```cpp
void test_repaint_happens_on_the_falling_edge_of_a_download(void) {
  // Guards the borrowed-scratch invariant: each decoded tile leaves the frame
  // sprite full of the decoder's DEFLATE window, so the frame MUST be recomposed
  // when a download stops -- and must NOT be recomposed every loop while the
  // 60 s retry gate holds downloadActive() false.
  // 4-tile view, tiles 1..3 succeed, tile 4 fails three times.
  TEST_ASSERT_EQUAL_INT(1, fake.draw_calls);
  for (int i = 0; i < 100; ++i) loopTick(state, ports);
  TEST_ASSERT_EQUAL_INT(1, fake.draw_calls);   // the gate must stay quiet
}
void test_wifi_down_grace_then_reconnect_cadence(void) {}
void test_adsb_is_not_refetched_faster_than_the_interval(void) {}
void test_a_site_tap_back_dates_the_fetch_clock_by_exactly_kAdsbMinRefetchMs(void) {}
void test_a_single_tap_changes_range_and_a_double_tap_changes_site(void) {}
void test_no_repaint_while_wifi_is_down(void) {}
```

### 4.5 Fill the named gaps in suites that already exist
**Priority 2. Effort low (3–4 h total). Value highest per hour — no new seams at all.**

| Add to | Case | Why |
|---|---|---|
| `test_terrain_fetch` | `test_download_active_tracks_the_flight_of_a_download` | Rank-1 gap. Assert `downloadActive()` after every `ensureGrid` across success/retry/abandon. |
| `test_terrain_fetch` | `test_a_grid_that_finishes_short_is_discarded_not_published` | Make `fakeDecode` skip one pixel row; assert false, `gridReady` false, `grid()` null, gate taken. `terrain.cpp:464-473` is dead code to the suite today. |
| `test_terrain_fetch` | `test_a_view_outside_the_projection_is_diagnosed_once` | `terrain.cpp:414-421`, `tile_count == 0`. |
| `test_terrain_fetch` | `test_the_poll_hook_is_invoked_during_every_request` | `s_poll_fn` is wired to `wifiLoop()`; dropping it kills the portal during fetches. |
| `test_terrain_fetch` | `test_a_centre_moved_by_less_than_the_epsilon_is_the_same_view` | `kCenterEpsilonDeg` = 1e-7: 5e-8 reuses, 5e-7 restarts. |
| `test_adsb` | `test_heading_falls_back_through_mag_heading_then_dir` | `adsb.cpp:34-51`. |
| `test_adsb` | `test_ground_speed_falls_back_to_tas_then_ias` | `adsb.cpp:53-59`. |
| `test_adsb` | `test_altitude_falls_back_to_alt_geom` | `adsb.cpp:99-101`. |
| `test_adsb` | `test_a_body_delivered_in_small_reads_parses_identically` | Drives `ElementScanner::readBytes` + pushback; needs a ~10-line `ChunkedBodyReader(n)`. |
| `test_settings` | `test_findAirport_normalises_case_and_rejects_non_alpha` | `airport_find.cpp:22-28`. |
| `test_settings` | `test_findAirport_finds_the_first_and_last_entries` | Pins that `kAirports` is sorted — a `build_large_airports.py` change could break the search silently. |
| `test_settings` | `test_a_six_site_list_round_trips_through_the_stored_string` | `persistSitesString`'s exactly-full buffer. |
| `test_settings` | `test_saveLocation_is_ignored_while_sites_are_configured` | `settings.cpp:184-187`. |
| `test_png` | `test_zero_length_idat_is_tolerated` | The brief lists it; **no such fixture symbol and no such case exist.** Add both or stop claiming it. |
| **new** `test_portal_params` | ~10 cases over all of `core::portal` | 161 LOC, pure, compiled into `native_test` with zero assertions. **Highest value-per-hour item in the document.** Pin especially `commit()`'s `saveSites`-before-`saveLocationFromStrings` ordering and the stage-vs-commit asymmetry in `applyValue`. |
| **new** `test_portal_http` | `urlDecode`, `htmlEscape`, `declaredContentLength`, `requestComplete`, `parseFormBody`, `applySubmission`, request-line dispatch | ~500 LOC of pure logic in `portal_server.cpp`, testable with only an internal header. Pin `urlDecode`'s `i + 2 < in.size()` end-of-string behaviour and `declaredContentLength`'s overflow short-circuit as-is (`TODO(P1)` if judged wrong). |
| **new** `test_boot_button` | The tap/long-press state machine | Pure arithmetic + booleans with two injectable deps. Write **one** table-driven suite and run it against both implementations; the device/native divergence (`wifi_setup_device.cpp:391-413` vs `button_sdl.cpp:65-101`) is exactly what it should surface. |

### 4.6 `test_range_geometry` — the duplicated screen constants
**Priority 2. Effort very low (1 h).** `radar_range.h` includes only `radar_theme.h`, which is constants + `extern` declarations — no LovyanGFX. A geometry test needs no `kColor*` definitions (they are declarations), so it can live in a new lightweight env.

```cpp
void test_fetch_radius_and_half_span_for_every_preset(void) {
  // Exact golden floats per preset, plus the invariant test_terrain relies on:
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 120.0f / 107.0f,
      ui::radar::terrainHalfSpanKm() / ui::radar::rangeCurrent().outer_km);
}
```

### 4.7 Prioritised summary

| # | Test | Env | Effort | Risk covered | New seam |
|---|---|---|---|---|---|
| 1 | 4.5 gap-fill in existing suites | existing | **low** | Ranks 1, 3 partly | **no** |
| 2 | 4.5 `test_portal_params` | `native_test` | low | 161 unpinned LOC | **no** |
| 3 | 4.2 `test_terrain_upsample` | new (no LGFX) | low | Rank 2 | expose anon-ns fns |
| 4 | 4.6 `test_range_geometry` | new (no LGFX) | very low | Rank 6 | **no** |
| 5 | 4.5 `test_portal_http` / `test_boot_button` | new (no LGFX) | low-med | Rank 6, platform 0 % | internal header |
| 6 | 4.3 `test_settings_migration` | `native_test` | medium | Rank 3 | pure `migrate()` |
| 7 | 4.1 `test_render_math` | new (no LGFX) | high | Ranks 2, 4, 6 | **`ui/render_math.cpp`** |
| 8 | 4.4 `test_shell` | new | high | Ranks 1, 5 | **`loopTick` + `Ports`** |

**Items 1–5 are ~10 h, need essentially no design decisions, and close the cheapest two thirds of the exposure. They should land before any refactor agent touches a source file.** Items 6–8 are seam work that is itself part of the refactor; they must be written in the same commit as the seam they depend on, never deferred.

---

## 5. Testability seams

### 5.1 Static census — reset reachability

**MEASURED: 44 module-level mutable statics + 5 `main.cpp` globals. No `*ForTest`/`resetForTests` entry point exists anywhere in `src/`.**

| File | Count | Reset reachable from a test? |
|---|---:|---|
| `main.cpp:26-30` | 5 | **No** — file not compiled by any test env |
| `core/tap_gesture.cpp:9-11` | 3 | **Yes** — `tapReset()`; `test_gesture` uses it in `setUp`. The only fully-resettable module. |
| `core/adsb.cpp:19-21` | 3 | Partly — `clear()` resets the count; `s_poll_fn` only via `setPollFn` |
| `core/settings.cpp:32-41` | 9 | **Partly and inconsistently** — `clearLocation()` resets location + sites; `unitsReset()` resets 3 bools *but deliberately not* `s_range_index`. **No full reset exists.** |
| `core/terrain.cpp:38-110` | 15 | **Mostly** — `clear()` resets the grid, both range keys and `s_prog.active`. It does **not** reset `s_fail_ms`, `s_prog`'s cursor fields, or the six mapping arrays. Sufficient in practice only because the gate keys on `s_fail_range_index == range_index` (set to `kNoRange`) and `progressMatches` gates on `s_prog.active`. |
| `core/portal_params.cpp:37-39` | 3 | **No reset function.** `commit()` clears them as a side effect. A new suite will need one. |
| `ui/radar_display.cpp:24-58` | 15 (+11 mutable global `kColor*`) | **No** |
| `ui/runway_overlay.cpp:20-26` | 6 | **No** (two `bool[kAirportCount]` arrays cleared per frame) |
| `ui/terrain_overlay.cpp:39-41` | 3 | **No** — `s_map_ready` is one-shot |
| `ui/display_font.cpp:8` | 1 | **No** — named in `platform.h:44-48` as reboot-unsafe |
| `ui/status_screens.cpp:33-38` | 5 | Effectively, via `statusScreenConnectingBegin()` |
| `platform/png_decode.cpp:56` | 1 | Injected setter, no reset |
| `platform/native/kv_json_file.cpp:45,115` | 2 function-local | **No** — one-shot path *and* one-shot document |
| `platform/native/wifi_setup_native.cpp:51-62` | 3 | **No** — `s_in_wifi_loop` can be left `true` if `bootButtonPollLongPress()` exits |
| `platform/native/portal_server.cpp:66-68` | 3 | `s_pending_ssid` is **never cleared** after consumption |
| `platform/native/button_sdl.cpp:32-36` | 5 | **No** for `s_boot_is_down`/`s_boot_down_ms`/`s_key_mapped` |
| `platform/device/wifi_setup_device.cpp:23-28,63-87` | 12 | **No** for `s_wm`, `s_wm_configured`, `s_boot_interrupt_attached`, `s_params[12]` (which would leak on a second `attachPortalParams`) |

### 5.2 What blocks host testing

1. **LovyanGFX at namespace scope.** `radar_display.cpp:57` (`LGFX_Sprite s_frame(&tft)`) and every `src/ui/*.cpp` including `ui/display.h`. On macOS that pulls SDL2's `#define main`, renaming Unity's entry point. **This single fact is why 1437 LOC of UI has no test.** The fix is partitioning, not mocking; `radar_theme.h` is already clean.
2. **`include/platform/wifi_setup.h` has no seam at all** — 11 global free functions, no injection, no reset hooks. Neither implementation can be substituted or reset between cases.
3. **Hardware calls inline in `loop()`.** Nothing can drive a tick.
4. **The one-shot KV path and document** (`kv_json_file.cpp:45,115`). Blocks any multi-scenario persistence test.
5. **`wifi_setup_device.cpp` bypasses `KeyValueStore`** for the `"wifi"`/`"portal"` key (raw `Preferences`, `:130-161`, `:366-371`). Even a perfect fake NVS would not reach it.
6. **Anonymous namespaces around pure functions** in `radar_display.cpp`, `runway_overlay.cpp`, `terrain_overlay.cpp`, `portal_server.cpp`, `status_screens.cpp`. Not a hard blocker on its own — but `#include`ing those `.cpp`s would drag LovyanGFX or sockets in, so in practice it is one.

### 5.3 Isolation and order dependence

| Suite | `setUp` | Isolated? |
|---|---|---|
| `test_adsb` | empty | **No** — `s_aircraft_count` carries over. Benign because every case starts with a `parseResponse`, and `test_a_short_reply_after_a_long_one_replaces_the_store` *depends* on the carry-over. Reordering is safe; deleting a case is not. |
| `test_geo` | empty | **Yes** — stateless |
| `test_gesture` | `tapReset()` | **Yes** — the only fully isolated suite |
| `test_settings` | empty | **No, a real ordering dependency.** `test_unitsReset_leaves_range_alone` calls `saveKmFromPortal("T")` + `rangeNext()`; `test_rangeNext_cycles_and_wraps` runs *before* it and defends itself by reading `start = rangeIndex()` first. `test_saveSites_*` each call `clearLocation()` as an ad-hoc reset. The scratch JSON is removed **once**, in `main()` — state accumulates across all 17 cases. Adding a case between the two range tests, or reordering them, can flip results. |
| `test_terrain` | empty | **Yes** — pure helpers only |
| `test_terrain_fetch` | `ct::clear()` + re-inject decoder + reset all fakes | **Yes, effectively** — the best-isolated stateful suite. `test_no_decoder_means_no_download` sets the decoder to `nullptr` and relies on `setUp` restoring it; it is the last case, so this is currently invisible either way. |
| `test_png` | full reset incl. guard-byte refill | **Yes** |
| `test_terrain_live` | `ct::clear()` + re-inject | Yes, modulo the network |

**Hidden cross-suite hazard.** Each PlatformIO suite is a separate binary, so env vars and statics do not leak between suites. But **only `test_settings` sets `PLANE_RADAR_SETTINGS`**. `test_terrain` links `kv_json_file.cpp` and includes `core/settings.h`; it happens to touch only `constexpr kRangePresets`, so it never opens the file. **Any new case added to `test_terrain` that calls a `core::settings` accessor with backing storage will silently read and rewrite `~/.plane-radar/settings.json`** — confirmed present on this machine. A live trap for the tests proposed in §4.

### 5.4 Are the existing native fakes sufficient?

| Fake | Where | Sufficient for the flagged modules? |
|---|---|---|
| Scripted `HttpClient::get` + hand-cranked `nowMs` | `test_terrain_fetch.cpp:220-248` | **Yes, and it is the model to copy.** Replacing the whole platform namespace at link time by simply *not building* `platform_native.cpp` is the cleanest seam in this project. Reuse verbatim for a shell test. |
| Injected `PngDecodeFn` | `terrain::setPngDecoder` | **Yes.** Real function-pointer seam, no heap, gnu++17-clean. |
| Injected `ScratchFn` + canary | `test_png.cpp:68-90` | **Yes** — the canary catches the one overflow that would not segfault on device. |
| `MemoryBodyReader` | `platform.h:134` | Adequate for `adsb::parseResponse`. **Insufficient** for `ElementScanner::readBytes` short-reads — it always returns the full count until exhaustion. A `ChunkedBodyReader(n)` is ~10 lines. |
| `kv_json_file.cpp` as a real KV | linked into `native_test` | **Insufficient** for migration work — one-shot path and document. |
| `platform_native.cpp` clock/log | linked into `native_test` | **Insufficient** — `nowMs()` is a real `steady_clock`, so anything time-dependent in `native_test` is untestable. `native_test_fetch` solves this by excluding the file; a shell test must do the same. |
| WiFi / portal / display / button | — | **Do not exist.** `wifi_setup_native.cpp`, `portal_server.cpp`, `button_sdl.cpp`, `display_native.cpp` are the *harness*, not fakes — they open sockets and SDL windows. There is no test double for the `platform/wifi_setup.h` seam at all. |

---

## 6. The generated-fixture workflow

### 6.1 Runs clean, and no drift — **MEASURED**

```
$ cp test/test_png/png_fixtures.h $SCRATCH/committed.h     # snapshot first
$ python3 scripts/gen_png_fixtures.py                       # Python 3.14.7
... 23 fixtures reported; wrote test/test_png/png_fixtures.h (152432 bytes)
python3 scripts/gen_png_fixtures.py  0.10s user 0.01s system 93% cpu 0.114 total
$ diff $SCRATCH/committed.h test/test_png/png_fixtures.h
IDENTICAL: no drift
```

**No drift.** 0.11 s, byte-identical output — the determinism claim in the docstring holds. My run left the file unchanged. **23 fixtures confirmed**, matching the brief.

**Qualification:** "up to date with the generator" is true of the **working tree**, not of **HEAD** (§1.5). `png_fixtures.h`, `test_png.cpp`, `png_decode.cpp` and `gen_png_fixtures.py` are modified together and uncommitted; regenerating from a clean HEAD checkout produces a *different* file, because the generator is itself one of the modified files. Commit all four as one baseline before any parallel agent starts.

The run's `empty_final_block ... adler SKIPPED` line is not a warning — it records that this fixture completes its raster in a non-final DEFLATE block, so the decoder never reaches the Adler-32, and `self_check` asserts exactly that.

### 6.2 Is `self_check()` non-vacuous?

**Largely yes — the strongest part of the test infrastructure — with one precise structural caveat.**

What it genuinely proves (`gen_png_fixtures.py:1192-1224`):

- Every decodable fixture is re-decoded by `reference_decode()`, which independently walks the chunk list, **verifies every chunk CRC**, concatenates IDATs, calls `zlib.decompress`, asserts the filtered length is `height × (1 + width × bpp)`, and compares byte for byte against the emitted raster. A wrong expected raster cannot be written.
- `first_block_type()` is asserted against each fixture's *declared* BTYPE, and the union over all fixtures against `⊇ {0, 1, 2}` — so a fixture set that quietly lost stored-block or dynamic-Huffman coverage fails the build. This is the check most fixture generators lack.
- `raster_completing_block(...).final == fixture.verifies_adler` — the generator *proves* whether each fixture reaches the Adler-32 at all. Subtle and genuinely valuable: it stops an Adler test from silently becoming a no-op.
- Every reject fixture gets a name-specific structural assertion in `check_rejected()` — greyscale really is colour type 0, interlaced really has `interlace == 1`, truncated really is shorter than its IDAT declares, and the corrupt-deflate fixture **still carries a valid chunk CRC** *and* genuinely fails `inflate_raw()`.
- `check_rejected()` ends in `raise AssertionError(f"no self-check for {fixture.name}")`, so **adding a reject fixture without a self-check fails the generator.** That is what keeps the check from decaying.
- Elsewhere it asserts its own construction: `:320` LEN/NLEN agreement, `:365` that its block walk agrees with `zlib.decompress`, `:775-782` that level 0 really produced stored blocks and the IDAT cut really lands mid-scanline, `:1081`/`:1112` that its corruption search actually found a byte flip with the required property.

**The caveat — a shared oracle on one axis.** `filter_row()` (`:107`) and `unfilter_row()` (`:115`) both call the *same* `predictor()` (`:87`). Filtering subtracts the prediction, unfiltering adds it back, so the round-trip is exact **regardless of whether `predictor()` matches RFC 2083**. A wrong Paeth tie-break or Average rounding would round-trip perfectly and `self_check()` would pass.

That axis is not unprotected — the *C decoder* implements the RFC independently, so `test_png` would go red. The operational consequence is a workflow rule worth writing down:

> **`python3 scripts/gen_png_fixtures.py` running clean proves less than it looks. It proves the fixtures are self-consistent. Only `pio test -e native_test_png` afterwards proves they match the RFC. Never regenerate without running the suite, and never "fix" a red `test_png` by regenerating.**

Two smaller notes: `reference_decode()` is non-interlaced-only by assertion (`:1177`), so the interlaced reject is checked structurally — correct and intentional. And the zero-length-IDAT fixture named in the brief does not exist in the working-tree header (§4.5); adding it is a generator change plus one case, never a hand edit.

---

## 7. CI

**MEASURED** by reading `.github/workflows/build.yml` and `release.yml` — the only two workflow files.

### 7.1 What CI runs

`build.yml`, on push to `main`/`master`, every PR, and manual dispatch:

| Job | Steps |
|---|---|
| `firmware` | `pio run -e supermini`; `pio run -t merge -e supermini`; upload binaries |
| `native` | `apt install libsdl2-dev libcurl4-openssl-dev`; `pio run -e native`; **`pio test -e native_test`** |

`release.yml`, on `v*` tags: builds and merges only. **It runs no tests at all** — a release can be cut from a commit whose tests fail.

### 7.2 What it would and would not catch

**It would catch a build break, on every configuration.** `pio run -e supermini` covers `+<*> -<platform/native/>` and `pio run -e native` covers `+<*> -<platform/device/>`, so between them **every file in `src/` is compiled**. That is real protection, and it is what enforces the portability invariant (an Arduino header leaking into `core/` fails the `native` job). For move (e) this is currently the *only* automated protection that exists.

**It would catch a behavioural regression only inside `native_test`.**

| Env | Cases | Run in CI? |
|---|---:|---|
| `native_test` | 72 | **yes** |
| `native_test_fetch` | 14 | **no** |
| `native_test_png` | 26 | **no** |
| `native_test_live` | 3 | no (correct — needs the network) |

**40 of the 112 offline cases (36 %) never run in CI** — and they are the wrong 40 to lose. `native_test_fetch` is the terrain download state machine, the best characterisation suite in the repo; `native_test_png` is the entire PNG decoder including every rejection path and the scratch canary. A refactor that broke the tile retry logic, the antimeridian resampler, the Adler-32 check or the scratch bounds **would go green in CI**. CI also does not verify that `png_fixtures.h` matches its generator, so fixture drift is invisible to it.

### 7.3 Recommended CI changes (small; they belong in P0)

1. In the `native` job, replace `pio test -e native_test` with `make test`. Cost: **+2.9 s** (**MEASURED**: cold `make test` is 7.40 s wall; the `native_test` env alone is ~4.5 s of that). There is no argument against this.
2. Add a drift gate:
   ```yaml
   - name: PNG fixtures are up to date with their generator
     run: |
       python3 scripts/gen_png_fixtures.py
       git diff --exit-code -- test/test_png/png_fixtures.h
   ```
3. Add `make test` to `release.yml` before the build, so no tagged release can ship a red tree.
4. Optionally a nightly `schedule:` job running `make test-live` (7.6 s **MEASURED**) — the only thing that would notice the AWS bucket changing its URL scheme. Keep it out of PR CI.

---

## Appendix — commands actually run

```
make test                                    # 3 envs, 112/112 pass, 4.47 s warm
rm -rf .pio/build/native_test{,_fetch,_png}  # build artifacts only
make test                                    # cold: 112/112 pass, 7.40 s
make test-live                               # 3/3 pass, 7.61 s, real AWS tiles
make test-build SUITE=test_geo               # compile only, 0 cases, 0.75 s
python3 scripts/gen_png_fixtures.py          # clean, 0.11 s, 23 fixtures
diff <pre-run snapshot> test/test_png/png_fixtures.h   # IDENTICAL, no drift
git status --porcelain                       # 7 modified (read-only inspection)
git show HEAD:test/test_png/png_fixtures.h   # 16 fixtures vs 23 in tree
git diff --stat test/test_png/png_fixtures.h # +578 lines uncommitted
wc -l  (all src/ and test/ sources)
grep -cE '^\s*RUN_TEST\('  (per test file)
grep -rn downloadActive test/                # zero hits
grep -rn 'ui::' test/                        # one comment, no code
grep -rn PLANE_RADAR_SETTINGS test/          # one call site, test_settings only
```

Read in full: `platformio.ini`, `Makefile`, `.github/workflows/*.yml`, `src/main.cpp`, all `src/core/*.cpp`, all `src/ui/*.cpp`, both KV backends, `platform_native.cpp`, `include/{config.h,core/platform.h,core/terrain.h,core/settings.h,ui/radar_range.h,ui/radar_theme.h,platform/png_decode.h}`, every file under `test/`, `scripts/gen_png_fixtures.py`, and (via a read-only sub-survey) `wifi_setup_device.cpp`, `portal_server.cpp`, `wifi_setup_native.cpp`, `http_arduino.cpp`, `http_curl.cpp`, `status_screens.cpp`, `display_font.cpp`, `button_sdl.cpp`, `platform_device.cpp`, `include/platform/wifi_setup.h`.

---

## Three things to act on immediately

1. **Commit the 7-file baseline before any agent starts.** 5 of the 26 PNG cases and the whole Adler-32 family exist only in the working tree.
2. **Change CI to `make test`.** 36 % of the offline suite — including the entire PNG decoder and the terrain state machine — currently never runs there. Two lines, +2.9 s.
3. **Do not treat P0 as a formality.** Items 1–5 in §4.7 are ~10 h with no design decisions and close the cheapest two thirds of the exposure. Items 6–8 must be written in the same commits as the seams they need, never after.
