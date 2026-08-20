# Findings 03 — Performance

Read-only investigation of `env:supermini` (ESP32-C3, RV32IMC, 160 MHz, **no FPU**). Nothing was modified. Evidence: `pio run -e supermini` + `riscv32-esp-elf-objdump -dr` on `.pio/build/supermini/src/**/*.o` and on `firmware.elf`, plus parsing the two generated data files (`src/core/large_airports_data.cpp`, `data/ui_font.vlw`) for real per-frame call counts at the default site.

**Out of scope by instruction:** the pristine-background / erase-by-rect optimization. Nothing below depends on it; all but PERF-06 also make that change cheaper if it lands.

## How the ms numbers were derived

Soft-float helpers are **ROM** symbols on this chip (`__mulsf3 = 0x40000854`, `__subdf3 = 0x40000894`, … per `esp32c3.rom.libgcc.ld`), absent from the ELF and not instruction-countable. Everything else was counted. A calibration was derived from the one stage with a MEASURED cost whose inner loop is entirely our own integer code:

| item | value | source |
|---|---|---|
| `bandAtPixel` | 21 instructions, tail-jumps into `bandForElevation` | DISASSEMBLY-CONFIRMED |
| `bandForElevation` | 3 instructions + **8 per band tested** (loop at `0x4201268c`) | DISASSEMBLY-CONFIRMED |
| per-pixel total incl. caller run-tracking | ≈ 50 instructions | DISASSEMBLY-CONFIRMED |
| pixels/frame | 57,600 | inspection |
| instructions/frame | ≈ 3.0 M | derived |
| MEASURED stage | 37 ms = 5.92 M cycles | given baseline |
| **effective rate** | **≈ 2.0 cycles/instruction** | derived |

All ESTIMATED ms below use **2 cycles/instruction**. Instruction counts are DISASSEMBLY-CONFIRMED.

## Baseline and assessed headroom

| Stage | MEASURED | Assessed remaining headroom | Where it goes |
|---|---|---|---|
| terrain background | 37 ms | **~20 ms** (→ ~15 ms) | 57,600 cross-TU calls into a 7-iteration linear band scan; PERF-01 |
| rings / runways / labels | **61 ms** | **~40 ms** (→ ~20 ms) | 2 anti-aliased crosshair wedge lines (~20 ms) + 1,704 double-precision airport distance tests (~19 ms); PERF-02/03/05/07/08 |
| aircraft | 6 ms | ~3 ms | one anti-aliased wedge line per target + duplicated `sinf`/`cosf`; PERF-06 |
| blit to panel | 23 ms | ~0 ms | SPI-bound, agreed irreducible |
| **total** | **128 ms** | **→ ~60 ms** | |

### Accounting for the 61 ms stage

Default site (Graz 47.0753/15.4062, default range index 1 = 20 NM ring, outer 49.4 km — `settings.cpp:30`). Counts from the generated airport table; instruction counts from the ELF.

| Sub-step | Work per frame | Instr | ESTIMATED ms |
|---|---|---|---|
| `drawCrosshairs` — 2 × `draw_wedgeline` | ~1,290 bbox px × ~1,300–1,900 instr | 1.7–2.5 M | **17–25** |
| runway airport in-range scan | **1,704** × `offsetKmFromCenter` (11 ROM soft-float calls + `sqrtf`) | ~1.96 M | **19** |
| 3 runway wedge lines | ~275 bbox px | 0.44 M | 5.5 |
| 8 × `drawCircle` (4 rings × 2 px) | ~155 octant steps × 8 spans × 227 instr | 0.28 M | 3.5 |
| `fillScreen` 240×240×16 bpp | — | ~0.12 M | 1.5 |
| runway ICAO label, drawn **3×** | 3 × 4 glyphs, ~183 colour-run rects each | 0.13 M | 1.6 |
| scale + site labels | 2 × ~190 colour-run rects | 0.10 M | 1.2 |
| 4 cardinal labels | 4 × ~44 colour-run rects | 0.04 M | 0.6 |
| centre dot (`fillSmoothCircle` r=2) | ~10 AA px × `sqrtf` | — | 0.2 |
| **accounted** | | | **~50–58** |

Against MEASURED 61 ms, 3–11 ms is unmodelled (`startWrite`/`endWrite` pairs, clip-rect set/clear per primitive, `text_width` walked twice per `drawString`).

Range/site sensitivity, computed from the generated table:

| preset | outer km | fetch r | airports in range | runways drawn | wedge bbox px | ICAO labels |
|---|---|---|---|---|---|---|
| 10 NM | 24.7 | 27.2 | 1 | 3 | ~510 | 1 |
| 20 NM (default) | 49.4 | 54.5 | 1 | 3 | ~275 | 1 |
| 40 NM | 98.8 | 108.9 | 1 | 3 | ~147 | 1 |
| 80 NM | 197.5 | 217.9 | 6 | 11 | ~508 | 6 |

The 1,704-call airport scan is **independent of location and range** — same 19 ms everywhere. In a dense metro (`kMaxAirportLabels = 32` can saturate) the ×3 label trick alone reaches ~20 ms.

## Ranked opportunities

| ID | Est. ms saved | Confidence | file:line | Idea |
|---|---|---|---|---|
| PERF-01 | 20 | high | `src/ui/terrain_overlay.cpp:64` | Kill the cross-TU call + linear band scan in the per-pixel path |
| PERF-03 | 20 | high | `src/ui/radar_display.cpp:543-548` | Crosshairs are axis-aligned: 6 rect fills instead of 2 anti-aliased wedge lines |
| PERF-02 | 19 | high | `src/ui/runway_overlay.cpp:272-294` | Airport distance test runs 1,704×/frame in `double`; cache it, keyed on site+range |
| PERF-05 | 1.1 (up to 29) | high | `src/ui/runway_overlay.cpp:171-173` | Fake-bold ICAO label draws the string 3× |
| PERF-06 | 3 | medium | `src/ui/radar_display.cpp:244-295` | Per-aircraft wedge line; `sinf`/`cosf` computed 4×; `noseTip` called twice |
| PERF-04 | 2 (+ halves PERF-02 residue) | high | `include/core/geo.h:19-24` | `Viewport::center_lat/lon` are `double`: every projection pays `__extendsfdf2`+`__subdf3`+`__truncdfsf2` ×2 |
| PERF-08 | 0.1 | medium | `src/ui/radar_display.cpp:525-541` | Rings: 8 passes because thickness derives from a float. Investigated; **not** the bottleneck |
| PERF-07 | 1.5 | high | `src/core/geo.cpp:16`, `src/ui/runway_overlay.cpp:83` | `px_per_km` re-divided per projection call; scale text + metrics re-derived per frame |
| PERF-09 | 15–60 ms of *download* latency | high | `src/core/terrain.cpp:268-279` | `terrariumElevation` uses `double` + `lround` per decoded grid sample |
| PERF-10 | 0 (analysis) | high | `platformio.ini:32-37` | `-std=gnu++11` is what actually compiles; `-O2` measured **not** to help |
| PERF-11 | 0 frame ms (0.5–2 s latency) | high | `http_arduino.cpp:36-47`, `tap_gesture.cpp:34` | Poll callback misses the TLS handshake; 500 ms double-tap window dominates anyway |

---

### PERF-01 — Terrain: remove the cross-TU call and linear band scan from the per-pixel path

- **Location:** `src/ui/terrain_overlay.cpp:64` (`bandAtPixel`) → `src/core/terrain.cpp:486` (`bandForElevation`), driven from `terrain_overlay.cpp:96` and `:109`.
- **Current cost:** MEASURED 37 ms. DISASSEMBLY-CONFIRMED it is a real call, not inlined:
  ```
  42017592: j 4201268a <core::terrain::bandForElevation(short, short const*, int)>   # tail jump out of bandAtPixel
  4201268c: addi / bge / slli / add / lh / blt / addi / j                            # 8 instructions PER BAND TESTED
  ```
  `bandAtPixel` = 21 instr, `bandForElevation` = 3 + 8·k (k = band floors tested; 1 at sea level, up to 7 in the Alps). With caller run-tracking ≈ 50 instr × 57,600 px ≈ 3.0 M instructions — exactly reproducing the MEASURED 37 ms.

  I also built `src/` at `-O2` (`PLATFORMIO_BUILD_SRC_FLAGS="-O2 -std=gnu++17"`) and re-disassembled: `bandAtPixel` stays 23 instr and **still tail-jumps**; `drawTerrainBackground` still has 2 calls to it; `bandForElevation` grows 11 → 17. DISASSEMBLY-CONFIRMED the optimizer cannot fix this — the TU boundary is the barrier. Algorithmic fix, not a flag fix.

- **Proposal, step 1 (exact, ~15 lines):** `radar_theme.h` already has the seven floors as `constexpr`, so specialise locally.
  ```cpp
  // terrain_overlay.cpp — replaces the call to core::terrain::bandForElevation
  // in the per-pixel path only. The generic, unit-tested definition stays.
  inline int bandOf(int32_t e) {                       // whole metres
    if (e < 1500) {
      if (e < 500)  return e < 1 ? -1 : (e < 200 ? 0 : 1);
      return e < 1000 ? 2 : 3;
    }
    if (e < 3000) return e < 2000 ? 4 : 5;
    return 6;
  }
  inline int bandAtPixel(const int32_t* row_elev, int x) {
    const int c = s_cell[x];
    const int32_t west = row_elev[c];
    return bandOf(west + (((row_elev[c + 1] - west) * s_frac[x]) >> kFracBits));
  }
  ```
  Back it with a test asserting `bandOf(e) == core::terrain::bandForElevation(e, kTerrainBandMinM, 7)` across the range. Per pixel goes ≈ 50 → ≈ 21 instructions (8 caller + 6 lerp + ~7 compare tree).

- **Proposal, step 2 (optional, still exact):** the lerp is **monotone inside one grid span**, and `s_cell`/`s_frac` are already frame-invariant. Band the 41 grid columns once per scanline; only resolve per-pixel inside spans whose ends differ.
  ```cpp
  int s_col_start[kGrid + 1];   // first x with s_cell[x] == c ; s_col_start[kGrid] = kSize

  int band_col[kGrid];
  for (int c = 0; c < kGrid; ++c) band_col[c] = bandOf(row_elev[c]);   // 41, not 240

  for (int c = 0; c + 1 < kGrid; ++c) {
    if (band_col[c] == band_col[c + 1]) {
      emitRun(band_col[c], s_col_start[c], s_col_start[c + 1]);  // zero per-pixel work
      continue;
    }
    for (int x = s_col_start[c]; x < s_col_start[c + 1]; ++x)     // only the crossings
      emitRun(bandAtPixel(row_elev, x), x, x + 1);
  }
  ```
  Bands are 200–1000 m apart, so a scanline crosses ~7–15: about 10 of 40 spans need per-pixel work. Band lookups per row: 240 → ≈ 41 + 60 = 101.

- **Estimated saving:** step 1 alone 37 → ~16 ms = **~20 ms** (ESTIMATED from the DISASSEMBLY-CONFIRMED 50 → 21 instr/px). Steps 1+2: 37 → ~10 ms = **~27 ms** (medium confidence, terrain-roughness dependent).
- **Accuracy impact:** none. No arithmetic changes; `kFracBits` and the integer lerp untouched; `bandOf` returns exactly what the scan returns. Step 2 is exact because monotonicity means a span with equal end bands cannot contain another band.
- **Risk:** none to borrowed-scratch or sprite lifetime — this loop only reads `core::terrain::Grid` and calls `drawFastHLine`. Step 2 adds `s_col_start[42]` = 168 B static. `bandForElevation` keeps its other callers and its test.
- **Verification:** `esp_timer_get_time()` around `drawTerrainBackground` at a fixed site with terrain loaded; plus a native-harness 240×240 pixel diff, which must be byte-identical.

---

### PERF-03 — Crosshairs: two anti-aliased wedge lines where six rect fills would be pixel-identical

- **Location:** `src/ui/radar_display.cpp:543-548`, called from `:619`.
- **Current cost:** `drawWideLine` is not a line drawer. `LGFXBase.hpp:287` forwards to `draw_wedgeline` → `draw_gradient_wedgeline`, which walks the line's **bounding box** and calls `wedgeLineDistance` per pixel. DISASSEMBLY-CONFIRMED from `firmware.elf`:
  ```
  lgfx::v1::wedgeLineDistance(float,float,float,float,float) — 110 instructions:
       9x __mulsf3   4x __addsf3   2x __subsf3   1x __divsf3
       1x __ltsf2    1x __gtsf2    + sqrtf
  lgfx::v1::LGFXBase::draw_gradient_wedgeline(...)            — 488 instructions
  void lgfx::v1::IPanel::effect<effect_fill_alpha>(...)       — 114 instructions
  lgfx::v1::LGFXBase::fillRectAlpha<...>                      —  36 instructions
  lgfx::v1::IPanel::writeFillRectAlphaPreclipped(...)         —  23 instructions
  lgfx::v1::Panel_Sprite::drawPixelPreclipped(...)            —  74 instructions
  ```
  `bax*bax + bay*bay` — the whole denominator — is loop-invariant and recomputed for every pixel inside the library. We can't fix that; we can avoid calling it. `sqrtf` is 34 + `__ieee754_sqrtf` 76 with a bit-by-bit loop (backward branch at `420c8758`) ≈ 285 executed instructions.

  Geometry from `radar_theme.h`: `kGridStrokeHalfWidth = 1.0f`, `kGridOuterRadius = 107`, centre (120,120). The library does `ar += 0.5f`, so the bbox is `x ∈ [119,121] × y ∈ [13,227]` for the vertical stroke and the transpose for the horizontal — **3 × 215 ≈ 645 evaluated pixels each, ~1,290 total**. Per pixel: 110 instr + ~18 ROM soft-float calls + one `sqrtf` (~285) + ~40 caller + either `drawPixel` (74) or `fillRectAlpha` (36+23+114 = 173) → **≈ 1,300–1,900 instructions**. ×1,290 = 1.7–2.5 M instr = **17–25 ms** (ESTIMATED).

  Cross-check: the same arithmetic predicts ~0.5–0.8 ms for one aircraft speed vector, and the MEASURED aircraft stage is 6 ms for a handful of targets plus triangles and tags. The two agree, which is why I trust this figure.

- **Proposal:** the crosshairs are axis-aligned, so the alpha profile is constant along the stroke: `alpha(x) = 1.5 − |x − 120|` → one opaque column and two half-alpha fringes. Three rect fills per stroke, not 645 distance evaluations.
  ```cpp
  void drawWideVLine(int cx, int y0, int len, uint16_t color) {
    // matches draw_wedgeline(cx, y0, cx, y0+len, 1.0f)
    s_draw->fillRectAlpha(cx - 1, y0, 1, len, 127, color);
    s_draw->fillRect     (cx,     y0, 1, len,      color);
    s_draw->fillRectAlpha(cx + 1, y0, 1, len, 127, color);
  }
  // drawWideHLine is the transpose.
  void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
    drawWideVLine(cx, cy - radius, 2 * radius + 1, color);
    drawWideHLine(cy, cx - radius, 2 * radius + 1, color);
  }
  ```
  `fillRectAlpha` takes `w`/`h`, so each fringe is **one** call covering 215 pixels instead of 215 one-pixel calls; the blend still happens per pixel inside `IPanel::effect<effect_fill_alpha>`, which is where it must happen (the crosshair sits on terrain, so the fringe must blend with what's underneath).
- **Estimated saving:** **~20 ms** (17–25 ms range, minus ~0.3 ms for six rect fills). High confidence on mechanism, medium on exact ms.
- **Accuracy impact:** intended pixel-identical. Two caveats to confirm on the native harness: the library computes `(uint8_t)(alpha * 255.0f)` = **127**, not 128; and the wedge's rounded end caps mean the first/last row of each stroke may differ by one half-alpha pixel. Both invisible at 240×240.
- **Risk:** none. No new memory, no sprite-lifetime or borrowed-scratch interaction. Must go through `s_draw` so `DrawScope` still targets the sprite.
- **Verification:** timer around `drawCrosshairs`; native pixel diff expecting zero differing pixels or only the two end caps.

---

### PERF-02 — Runway overlay: the airport in-range test runs 1,704 times per frame, in `double`

- **Location:** `src/ui/runway_overlay.cpp:272-294`, via `:73-80` (`offsetKmFromCenter`) and `:71` (`e7ToDeg`).
- **Current cost:** two compounding defects.

  **1. The frame-local cache never memoises a negative.**
  ```cpp
  if (!s_in_range[ap_idx]) {                 // false also means "not yet tested"
    … offsetKmFromCenter(…) …
    s_in_range[ap_idx] = (dist_km <= radius_km);
  }
  if (!s_in_range[ap_idx]) continue;
  ```
  For an out-of-range airport `s_in_range` stays `false`, so **every one of its runways recomputes the distance**. Parsing `large_airports_data.cpp` (1,166 airports / 1,706 runways) at the default site gives 1 airport in range with 3 runways → **1,704 calls per frame**, not 1,166 and certainly not 1.

  **2. Each call is double-precision.** DISASSEMBLY-CONFIRMED from `runway_overlay.cpp.o`:
  ```
  offsetKmFromCenter [clone .constprop.3] — 68 instructions, and:
      2x __extendsfdf2   2x __subdf3   2x __truncdfsf2
      4x __mulsf3        1x __addsf3   1x sqrtf
      + non-inlined core::settings::lat() and core::settings::lon()
  ```
  The `double` chain exists purely because `core::settings::lon()` returns `double` (`include/core/settings.h:75-76`), so `lon - core::settings::lon()` promotes a `float` to `double`, subtracts in `double`, and truncates back.

  Per call ≈ 68 + 2·25 + 2·100 + 2·40 + 4·45 + 50 + 285 + 2 `e7ToDeg` (`__floatsisf`+`__mulsf3` each) + one `__lesf2` ≈ 1,150 instructions. × 1,704 = **1.96 M instructions ≈ 19 ms** (ESTIMATED). Also: the two `bool[1166]` arrays are cleared with a 2,332-byte double loop every frame (`:267-270`).

- **Proposal:** the set is a pure function of (site lat, site lon, range index). Cache across frames; make the miss path cheap.
  ```cpp
  uint8_t s_in_range_bits[(data::large_airports::kAirportCount + 7) / 8];  // 146 B
  uint8_t s_label_done_bits[(data::large_airports::kAirportCount + 7) / 8];
  double  s_cache_lat = 1e9, s_cache_lon = 1e9;
  uint8_t s_cache_range = 0xFF;

  void ensureInRangeCache() {
    const double lat = core::settings::lat(), lon = core::settings::lon();
    const uint8_t ri = core::settings::rangeIndex();
    if (lat == s_cache_lat && lon == s_cache_lon && ri == s_cache_range) return;
    s_cache_lat = lat; s_cache_lon = lon; s_cache_range = ri;

    // hoist the centre to float ONCE; compare squared distances so no sqrtf at all.
    const float clat = (float)lat, clon = (float)lon;
    const float r = radar::fetchRadiusKm();
    const float r2 = r * r;
    memset(s_in_range_bits, 0, sizeof(s_in_range_bits));
    for (size_t i = 0; i < data::large_airports::kAirportCount; ++i) {
      const auto& ap = data::large_airports::kAirports[i];
      const float dx = (e7ToDeg(ap.lon_e7) - clon) * kKmPerDeg;
      const float dy = (e7ToDeg(ap.lat_e7) - clat) * kKmPerDeg;
      if (dx * dx + dy * dy <= r2) setBit(s_in_range_bits, i);
    }
  }
  ```
  The runway loop becomes `if (!testBit(s_in_range_bits, rw.airport_idx)) continue;`. Two things happen at once: the scan runs **once per site/range change**, and when it does run it is 1,166 × ≈ 250 instructions (no `double`, no `sqrtf`) ≈ 3 ms, paid on a tap.
- **Estimated saving:** **~19 ms per frame** (ESTIMATED; the 1,704-call count and the 11-soft-float body are hard facts, so high confidence — only cycles/instruction is estimated). One-off 3 ms on a tap, invisible against the 500 ms tap window (PERF-11).
- **Accuracy impact:** `float` centre costs ~1e-5 ° ≈ 1 m at the radar centre; the threshold it feeds is 27–218 km. Dropping `sqrtf` for a squared comparison is **exactly** equivalent for non-negative operands; at 80 NM `dx²+dy²` peaks near 4.7e4 km², far inside `float`'s exact range. This preserves the existing flat-earth `111 km/deg` on *both* axes including the missing `cos(lat)` factor on longitude — a fidelity question for another investigator; changing it here would move which airports appear.
- **Risk:** none to borrowed-scratch (static arrays, not sprite memory) or sprite lifetime. Static RAM **drops** ~2.0 KB (two 1,166-byte `bool` arrays → two 146-byte bitmaps), i.e. 2 KB back on the heap the TLS session competes for. The one real hazard is invalidation: the key must cover site change (`siteNext`), portal save (`saveLocationFromStrings`), `clearLocation`, and `rangeNext`. Keying on the actual values rather than on change notifications makes that safe by construction.
- **Verification:** timer around `drawLargeAirportRunways` across ten frames at a fixed site — the first frame after a tap shows the one-off scan, the rest do not. Native pixel diff at all four presets and a couple of hand-set coordinates proves the visible runway set is unchanged.

---

### PERF-05 — The ICAO runway label is rasterised three times for a fake-bold effect

- **Location:** `src/ui/runway_overlay.cpp:171-173`
  ```cpp
  gfx.drawString(ident, mx - 1, my);
  gfx.drawString(ident, mx + 1, my);
  gfx.drawString(ident, mx,     my);
  ```
- **Current cost:** DISASSEMBLY-CONFIRMED the VLW path is a colour-run rasteriser: `VLWfont::drawChar` is **662 instructions** and issues one `writeFillRect` per run of equal alpha per glyph row (`lgfx_fonts.cpp:2010-2023`); `Panel_Sprite::writeFillRectPreclipped` is **203 instructions**, `LGFXBase::writeFillRect` another 27.

  I parsed `data/ui_font.vlw` (95 glyphs, Noto Sans Bold 15, ascent 12, descent 4) and replayed that exact run-splitting logic at the sizes the code picks: a 4-character ICAO label emits **~183 colour-run rects**, so one label ≈ 4×662 + 183×230 ≈ 44,700 instructions ≈ 0.45 ms (ESTIMATED). Three times = ~1.35 ms per airport. `drawString` also walks `text_width` twice per call, which the ×3 triples too.

  Default site: 1.1 ms wasted. With the label list full (`kMaxAirportLabels = 32`) that is 96 rasterisations ≈ **43 ms**, of which ~29 ms is duplication.
- **Proposal:** the font is already `FreeSansBold` / Noto Sans **Bold**. Draw once; if extra weight is wanted, get it from the size (`findVlwSizeForHeight` already exists) or two passes.
  ```cpp
  gfx.setTextDatum(textdatum_t::bottom_center);
  gfx.fillRect(left, top, tw + kPadX * 2, th + kPadY, radar::kColorBackground);
  gfx.setTextColor(radar::kColorRunwayLabel, radar::kColorBackground);
  gfx.drawString(ident, mx, my);          // one pass
  ```
- **Estimated saving:** **~1.1 ms** at the default site, **up to ~29 ms** with a full label list (ESTIMATED; the ~183-rects figure comes from the real font file, so the 3→1 ratio is solid).
- **Accuracy impact:** not numeric — a deliberate visual change; the label gets thinner. Needs a look on the native harness and belongs to whoever owns the visual spec.
- **Risk:** none technically. Note the background patch is sized from the single-draw width (`tw`), so it already does not cover the ±1 smear — the outer smear pixels land directly on terrain today. Removing them arguably makes the label cleaner.
- **Verification:** native capture at a dense site (portal coordinates in the Low Countries) with a timer around `drawLargeAirportRunways`, 3-pass vs 1-pass.

---

### PERF-06 — Per-aircraft: a wedge line for the speed vector, and `sinf`/`cosf` computed twice

- **Location:** `src/ui/radar_display.cpp:244-295` (`noseTip`, `drawHeadingTriangle`, `drawSpeedVector`), called from `:467-469`.
- **Current cost:** three things, DISASSEMBLY-CONFIRMED:
  1. `drawSpeedVector` ends in `drawWideLine` → `draw_wedgeline`. By the PERF-03 arithmetic a 20 px vector with half-width 1 evaluates ~4×20 ≈ 80 bbox pixels ≈ 0.5–0.8 ms **per aircraft** — most of the MEASURED 6 ms stage.
  2. `noseTip` (41 instr + `sinf` + `cosf` + 3×`__mulsf3` + 2×`lroundf`) is called **twice** per aircraft (`:282` and `:259`), and `drawHeadingTriangle` then computes `sinf(rad)`/`cosf(rad)` for the *same* heading a third and fourth time at `:255-256`. `sinf` = 42 + `__kernel_sinf` 123 ≈ 165 executed instructions; `cosf` likewise. ~660 instructions/aircraft of pure duplication.
  3. `drawBeyondRingDot` uses `fillSmoothCircle(r=4)` → `fillSmoothRoundRect` (188 instr) which runs `sqrtf` per anti-aliased edge pixel — ~20 `sqrtf` per rim dot.
- **Proposal:**
  ```cpp
  // (a) one trig evaluation per aircraft, from a table.
  extern const int16_t kSinQ15[360];        // sin(d) * 32767, generated
  inline int16_t sinDeg(float d) { return kSinQ15[wrap360(d)]; }
  inline int16_t cosDeg(float d) { return kSinQ15[wrap360(d + 90.0f)]; }

  // (b) compute once and pass down
  struct Trig { int32_t s, c; };            // Q15
  void drawAircraftSymbol(int cx, int cy, Trig t, ...);   // nose, tail, wings, vector
  const int tip_x = cx + ((t.s * radar::kAircraftNoseLenPx + 16384) >> 15);
  const int tip_y = cy - ((t.c * radar::kAircraftNoseLenPx + 16384) >> 15);

  // (c) speed vector: two Bresenham lines instead of a wedge
  s_draw->drawLine(tip_x, tip_y, ex, ey, color);
  s_draw->drawLine(tip_x + nx, tip_y + ny, ex + nx, ey + ny, color);  // nx,ny = unit normal
  ```
- **Estimated saving:** **~3 ms** of the MEASURED 6 ms at typical target counts (~0.5 ms/aircraft for the wedge, ~0.01 ms for the trig). Scales to `kMaxAircraft = 64`. Worth doing regardless of the deferred change — it makes the per-aircraft draw cheaper either way, which is precisely what that change makes the hot path.
- **Accuracy impact:** a Q15 sine table at whole-degree resolution is accurate to 3e-5 in amplitude and 0.5° in angle; over the longest thing drawn from it (~30 px vector) 0.5° is 0.26 px, and endpoints are `lround`ed anyway. Replacing the wedge with two Bresenham lines **does** lose anti-aliasing on diagonal vectors — a visual judgement; if it reads badly the trig half stands alone.
- **Risk:** none to borrowed-scratch. 720 B–1.4 KB of `const` table goes in flash.
- **Verification:** timer around `drawAircraft` with a replayed aircraft list (the native harness can feed a canned adsb.fi response) plus a pixel diff of the symbols.

---

### PERF-04 — `Viewport::center_lat/lon` are `double`, and every projection call pays for it

- **Location:** `include/core/geo.h:19-24`; consumed by `src/core/geo.cpp:7-13` and re-derived independently in `src/ui/runway_overlay.cpp:73-80`. `core::settings::lat()`/`lon()` are `double` (`include/core/settings.h:75-76`).
- **Current cost:** DISASSEMBLY-CONFIRMED — `core::geo::offsetKmFromCenter` is 63 instructions issuing, in order:
  ```
  __extendsfdf2, __subdf3, __truncdfsf2, __mulsf3,      # dx_km
  __extendsfdf2, __subdf3, __truncdfsf2, __mulsf3,      # dy_km
  __mulsf3, __mulsf3, __addsf3, sqrtf                   # dist_km
  ```
  Eleven soft-float calls plus `sqrtf`, of which **six exist only because the centre is `double`**. `latLonToScreen` (43 instr) adds `__floatsisf` + `__divsf3` + 2×`__mulsf3` + 2×`lroundf`.

  Call sites per frame: `drawAircraft` calls `offsetKmFromCenter` once per target for the visibility test (`:432`), then `latLonToScreen` (another) for on-disc targets or `rimPointForDistantTarget` (another, plus `atan2f`+`sinf`+`cosf`) for off-disc ones — 2–3 per aircraft. Plus `runway_overlay.cpp`'s copy 1,704 times (PERF-02) and twice per drawn runway. The `double` half is ~330 of the ~1,150 instructions per call; ~2 ms/frame at typical traffic (ESTIMATED).

  Where `double` is legitimately needed: `settings::parseCoord`/`validLatLon`, the NVS round-trip, `core::terrain`'s Web Mercator maths (`terrain.cpp:281-306` — `log`, `sin`, `nextafter`) and `gridReady`'s 1e-7 ° epsilon. **None of those are per-frame.** The projection is.
- **Proposal:** keep `double` at the boundary, convert once where the viewport is built.
  ```cpp
  struct Viewport {
    float center_lat = 0.0f;     // was double
    float center_lon = 0.0f;
    int   center_x = 0, center_y = 0;
    int   outer_radius_px = 0;
    float outer_km = 0.0f;
    float px_per_km = 0.0f;      // see PERF-07 — precomputed, not re-divided
  };
  // radar_display.cpp — the one conversion point
  vp.center_lat = static_cast<float>(core::settings::lat());
  vp.center_lon = static_cast<float>(core::settings::lon());
  ```
  `offsetKmFromCenter` then compiles to `__subsf3, __mulsf3` ×2 plus the distance — 5 soft-float calls instead of 11.
- **Estimated saving:** **~2 ms/frame** at typical traffic (ESTIMATED), plus it is a prerequisite for PERF-02's cheap one-off scan.
- **Accuracy impact:** `float` holds ~7 significant digits, so latitude 47.0753 carries ~1e-5 ° ≈ 1.1 m; the difference `lat − center_lat` (both ~47) loses to cancellation down to ~1e-4 ° ≈ 11 m. One screen pixel is `outer_km / 107` = **231 m at 20 NM and 1.85 km at 80 NM** — the error is 1/20 of a pixel at the tightest zoom. Aircraft coordinates are *already* `float` (`aircraft.h:14-15`), so the current `double` centre is precision on one operand of a subtraction whose other operand is `float`. `test_geo` uses `UNITY_INCLUDE_DOUBLE` for lat/lon asserts and needs tolerances widened to ~1e-4 °.
- **Risk:** none to borrowed-scratch or sprite lifetime. Care is at the seam: `core::settings`, `core::terrain` and the portal keep their `double`s; only `Viewport` narrows. `test_geo` tolerance changes are the intended signal.
- **Verification:** `pio test -e native_test` with widened tolerances, then a native pixel diff with a canned aircraft list — projected integer pixel coordinates should be identical for every target at every preset.

---

### PERF-08 — Rings drawn as eight `drawCircle` passes because the thickness comes from a float

- **Location:** `src/ui/radar_display.cpp:525-541`
  ```cpp
  const int thickness = std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) s_draw->drawCircle(cx, cy, r - i, color);
  ```
- **Current cost:** `kRingCount = 4` × thickness 2 = **8** `drawCircle` calls. DISASSEMBLY-CONFIRMED `LGFXBase::drawCircle` is 157 instructions of integer Bresenham (**no float** — good), but each octant step issues 8 span writes through `writeFastHLine` (24) + `Panel_Sprite::writeFillRectPreclipped` (**203**). Spans are 1–3 px, so that 203-instruction body is almost all fixed overhead. Octant steps ≈ 0.293·r over r ∈ {107,106,80,79,53,52,26,25} ≈ 155 × 8 × 227 ≈ 0.28 M instr ≈ **3.5 ms** (ESTIMATED).
- **Proposal:** `kGridStrokeHalfWidth * 2.0f` is `constexpr`-evaluable — make `thickness` a `constexpr int` so the float multiply and `std::max` vanish. A span-table cache was considered and **rejected**: the useful form (per-row inner/outer x for 4 rings) is ~1.9 KB static and produces 1,920 spans, no better than today, while a full span list is ~7.4 KB against a ~45 KB heap. **Recommendation: option 1 only; treat the rings as already cheap enough.**
- **Estimated saving:** ~0.1 ms (ESTIMATED). Listed mainly to record that the rings were investigated and are **not** where the 61 ms is — easy to assume otherwise.
- **Accuracy impact:** none. **Risk:** none.
- **Verification:** covered by any whole-stage timer.

---

### PERF-07 — Per-frame re-derivation of frame-invariant values

- **Location:** `src/core/geo.cpp:16-17` and `src/ui/runway_overlay.cpp:83-85` (`px_per_km`); `src/core/geo.cpp:26-33` (`innerRingMaxKm`, recomputed inside `isInsideOuterRingKm` on every call); `src/ui/radar_display.cpp:574-579` (scale text per frame); `:507-523`, `:581-597` (label width/height remeasured per frame); `:536-541` (ring radii per frame).
- **Current cost:** DISASSEMBLY-CONFIRMED that each `latLonToScreen` pays `__floatsisf` + `__divsf3` to recompute `outer_radius_px / outer_km` — a value that changes only on a range tap; `runway_overlay.cpp:83` additionally calls the non-inlined `rangeCurrent()` for it. `isInsideOuterRingKm` calls `innerRingMaxKm` = 2×`__floatsisf` + `__divsf3` + `__mulsf3`, once per aircraft and per rim-dot test.

  `drawScaleLabelWithBackground` and `drawSiteLabelWithBackground` are 64 instructions each and both contain `__floatsisf` + `__mulsf3` + `__fixsfsi` — that is `LGFXBase::fontHeight()` (`LGFXBase.hpp:709`), `(int32_t)(_font_metrics.height * _text_style.size_y)`: a soft-float multiply every time anyone asks how tall the font is. `textWidth` (115 instr) does the same for `size_x`. Together perhaps **1.5 ms/frame** (ESTIMATED) — small individually; worth fixing because it is free and sits on the paths PERF-02/04/06 also touch.
- **Proposal:** one small invalidated-on-tap struct.
  ```cpp
  struct FrameConst {
    core::geo::Viewport vp;          // includes px_per_km (PERF-04)
    float  inner_ring_max_km;        // innerRingMaxKm(vp, kAircraftInsideRingInsetPx)
    int    ring_r[radar::kRingCount];
    char   scale_text[12];
    int    scale_w, scale_h, site_w, site_h;
    uint8_t range_index; double lat, lon; bool use_km;   // the key
  };
  FrameConst s_fc;
  void ensureFrameConst() {
    if (s_fc.range_index == core::settings::rangeIndex() &&
        s_fc.lat == core::settings::lat() && s_fc.lon == core::settings::lon() &&
        s_fc.use_km == core::settings::useKm()) return;
    … rebuild, including the one formatCurrentRing3Label and the two textWidth calls …
  }
  ```
  and give `core::geo::isInsideOuterRingKm` an overload taking the precomputed `inner_ring_max_km` so it stops re-dividing per target.
- **Estimated saving:** **~1.5 ms/frame** (ESTIMATED). **Accuracy impact:** none — same values, computed once.
- **Risk:** none to borrowed-scratch. Same invalidation discipline as PERF-02: key on values, not notifications. `initLabelMetrics` already demonstrates the once-only pattern (`s_label_metrics_ready`), so this extends an existing convention. Note `initPalette()` runs twice per frame (`radarDisplayDraw:658` and again inside `drawStaticGrid:620`) — noise (~20 `color565`), but belongs in the same cleanup.
- **Verification:** whole-stage timer plus a native pixel diff (must be byte-identical).

---

### PERF-09 — `terrariumElevation` does `double` arithmetic per decoded grid sample

- **Location:** `src/core/terrain.cpp:268-279`, called from `onPixel` (`:201`).
- **Current cost:** `core/terrain.cpp.o` carries 83 soft-float relocations — second-highest of any of our objects — and this is the only one on a per-sample path.
  ```cpp
  const double meters = static_cast<double>(r) * 256.0 + g + b / 256.0 - 32768.0;
  … lround(meters)
  ```
  That is `__floatunsidf`, `__muldf3`, `__adddf3`, `__floatunsidf`, `__divdf3`, `__adddf3`, `__subdf3`, `lround` — eight `double` calls including the two most expensive helpers on the chip. `onPixel`'s row/column early-outs mean it runs for ~1/36 of a tile's 65,536 pixels ≈ **1,750 times per tile**, 1–4 tiles per view. At ~720 instructions per call ≈ 1.3 M instructions ≈ **15 ms per tile** (ESTIMATED). **Not** on the frame path — it runs inside the blocking HTTP body read, so it lands on main-loop latency.
- **Proposal:** the expression is exactly integer.
  ```cpp
  int16_t terrariumElevation(uint8_t r, uint8_t g, uint8_t b) {
    // height_m = R*256 + G + B/256 - 32768, rounded to whole metres.
    const int32_t whole = static_cast<int32_t>(r) * 256 + g - 32768;
    const int32_t m = whole + (b >= 128 ? 1 : 0);
    return m > INT16_MAX ? INT16_MAX : static_cast<int16_t>(m);
  }
  ```
- **Estimated saving:** ~15 ms per tile, 15–60 ms per download (ESTIMATED). Zero frame ms. Two lines, with a unit test already in place.
- **Accuracy impact:** one edge case. `lround` rounds half **away from zero**, so for a negative total with `b == 128` exactly the old code returns `whole` while the sketch returns `whole + 1` — a **1 m** difference, at one of 256 blue values, only below sea level. Bands are 200–1000 m apart; it cannot move a contour by a pixel. For bit-exactness with existing vectors, spell the tie out: `+ ((b > 128 || (b == 128 && whole >= 0)) ? 1 : 0)`.
- **Risk:** none. No memory, no sprite interaction. `test_terrain` and `test_terrain_live` both cover it.
- **Verification:** `pio test -e native_test`, then `make test-live` to decode real tiles and confirm the grid is unchanged; time a tile decode with `esp_timer_get_time()` around `HttpClient::get`.

---

### PERF-10 — Compiler settings: what is actually in effect

- **Location:** `platformio.ini:32-37` (`env:supermini` `build_flags`) vs `:74`, `:114`, `:148`, `:199` (native and test envs, all `-O0 -g`).
- **Three findings, all COMMAND-LINE / DISASSEMBLY-CONFIRMED:**

  **1. `supermini` compiles at `-Os` and `-std=gnu++11` — not `gnu++17`.** The real command line (`pio run -e supermini -v`):
  ```
  riscv32-esp-elf-g++ … -std=gnu++17 -march=rv32imc -std=gnu++11 -fexceptions
    -fno-rtti -Os -ffunction-sections -fdata-sections … -fstack-protector
    -fstrict-volatile-bitfields -fno-jump-tables -fno-tree-switch-conversion …
  ```
  PlatformIO puts `build_flags` **before** the framework's flags and GCC takes the **last** `-std`. Verified directly: `riscv32-esp-elf-g++ -std=gnu++17 -std=gnu++11 -c` with `static_assert(__cplusplus == 201103L)` compiles clean on this toolchain (8.4.0, crosstool-NG esp-2021r2-patch5). C++17 constructs that are GNU extensions in C++11 mode — e.g. `namespace ui::runway {` at `runway_overlay.cpp:14` — still compile, which is why this has gone unnoticed. By the same ordering, an `-O2` in `build_flags` would be **overridden** by the framework's `-Os`.

  **2. `build_src_flags` is the only lever that wins, and it works.** Verified with `PLATFORMIO_BUILD_SRC_FLAGS="-O2"`: the flag lands **after** `-Os` (position 30 vs 10 on the command line), so it applies to `src/` only and leaves LovyanGFX at `-Os`. Correct form if wanted:
  ```ini
  ; env:supermini
  build_src_flags = -O2 -std=gnu++17
  ```

  **3. `-O2` measured not to help the hot loop.** Built `src/` at `-O2 -std=gnu++17` and re-disassembled. Flash and RAM were unchanged to the byte vs the `-Os` control (`RAM 58,668 B / Flash 1,264,250 B`), and the terrain inner loop is unmoved:

  | function | `-Os` | `-O2` |
  |---|---|---|
  | `bandAtPixel` | 23 | 23 (still tail-jumps out) |
  | `bandForElevation` | 11 | 17 |
  | `drawTerrainBackground` | 156 | 172 (still 2 calls to `bandAtPixel`) |
  | `offsetKmFromCenter` (geo) | 63 | 63 (the `double` chain stays) |
  | `drawAircraft` | 587 | 800 |

  `-O2` cannot inline across the `terrain_overlay.cpp` / `terrain.cpp` boundary and cannot see through `double`. **The flag is not the fix; PERF-01 and PERF-04 are.**

- **Proposal:** don't chase flags. Fixing the `-std` ordering by moving `-std=gnu++17` into `build_src_flags` is worth doing for **correctness** (the file's own comment at `:5-7` and the native envs believe it is C++17), not speed. **Do not** propose `-flto`: GCC 8.4 supports it, but the ESP-IDF static libraries under `framework-arduinoespressif32/tools/sdk/esp32c3/lib` are not LTO objects, so it would only cover `src/`, and everything it would buy there is obtainable visibly and testably by inlining the band lookup (PERF-01). `-fno-jump-tables` / `-fno-tree-switch-conversion` are ESP-IDF flash-cache-safety flags; leave them. `-fstack-protector` is real (`renderFrame` and `Panel_Sprite::writeFillRectPreclipped` both carry `__stack_chk_guard`/`__stack_chk_fail`) but removing it via `build_unflags` trades security for a fraction of a millisecond and is **not** recommended.
- **Estimated saving:** **0 ms.** This exists to close the question and stop `-O2` being mistaken for a fix.
- **Risk:** switching `src/` to `-O2` changes codegen project-wide for no measured gain; `-std=gnu++17` may surface new warnings/errors in code that has been compiling as C++11-with-extensions.
- **Verification:** `pio run -e supermini -v | grep -oE '\-O[0-9s]'` for ordering; `objdump` diff of hot functions as above.

---

### PERF-11 — Where BOOT-button latency is actually lost (mostly not the 128 ms)

- **Location:** `src/main.cpp:159-195` (`loop`), `:88-93` (`pollWifiAndTaps`), `src/platform/device/http_arduino.cpp:33-49` and `:93-125`, `src/core/tap_gesture.cpp:27-41`.
- **Three terms, largest first:**

  **1. The 500 ms double-tap window, by design.** `tapPoll` (`tap_gesture.cpp:34-38`) will not report `kSingle` until `now − first_tap ≥ config::kDoubleTapWindowMs` = **500 ms** (`include/config.h:37`). A single tap therefore *cannot* act sooner than 500 ms, ever. The 128 ms frame is a 25 % aggravation on top of an intentional 500 ms floor, not the dominant term. (A double tap is prompt — `tapPress` sets `s_ready = kDouble` on the second press.)

  **2. The TLS handshake is not covered by the poll callback.** `setPollFn` is wired for both consumers (`main.cpp:149-150`) and `http_arduino.cpp` polls around the connect retry loop and around every body refill — but look where:
  ```cpp
  while (millis() < deadline) {
    poll(fn);                      // polled here, BEFORE the call
    const int code = http.GET();   // DNS + TCP + full TLS handshake + request
                                   // + response headers, all inside, no poll
    …
  }
  ```
  `http.GET()` runs the mbedTLS handshake to completion synchronously. `setConnectTimeout(kConnectAttemptMs)` = 200 ms bounds only the TCP connect. On an ESP32-C3 with software crypto an ECDHE handshake is commonly **0.5–2 s**, and a fresh one happens on *every* request: `HttpClient::get` constructs a new `WiFiClientSecure` per call (`:141`), so every 10 s ADS-B poll and every terrain tile pays one. During that window nothing polls — no `wifiLoop()`, no `tapPress()`. The tap is not *lost* (`onBootButtonIsr` at `wifi_setup_device.cpp:30-45` latches it in an ISR under a critical section) but its dispatch waits. Worse: `pollWifiAndTaps` only calls `tapPress`, never `tapPoll`, so the 500 ms window expires *inside* the blocked region and the action still waits for the next `handleBootButton()` in `loop()`.

  **3. The 128 ms frame and `sleepMs(10)`.** `loop` runs `handleBootButton()` first, so worst case is one render plus the sleep ≈ 138 ms.

  Worst-case single-tap-to-action ≈ **500 ms (window) + up to ~2 s (handshake) + 138 ms (frame + sleep)**. The 128 ms is roughly 5 % of it.

- **Proposal:** the frame work is covered by PERF-01/02/03. For the rest:

  **(a) Reuse the TLS session** — turns a full handshake into an abbreviated one:
  ```cpp
  static sslsession s_session;             // file scope; per-host would be better
  bool HttpClient::get(...) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setSession(&s_session);          // resume if we have a ticket
    …
  }
  ```
  Also consider `http.setReuse(true)` and hoisting the `HTTPClient` so the four tiles of one view share a connection instead of handshaking four times.

  **(b) Poll inside the handshake, or move fetches off the loop.** There is no callback seam inside `http.GET()`. Two honest options: `esp_http_client` in non-blocking mode, or a dedicated FreeRTOS fetch task with `loop()` reduced to input + render. Both are larger than anything else here and should be a separate decision.

  **(c) Cheap and immediate:** have `pollWifiAndTaps` also service the gesture machine and set a flag `loop()` consumes, so the 500 ms is not spent inside a blocked region:
  ```cpp
  void pollWifiAndTaps() {
    wifiLoop();
    if (bootButtonConsumeTap()) core::gesture::tapPress(pf::nowMs());
    switch (core::gesture::tapPoll(pf::nowMs())) {   // decide during the block
      case core::gesture::Tap::kSingle: g_pending = Pending::kRange; break;
      case core::gesture::Tap::kDouble: g_pending = Pending::kSite;  break;
      default: break;
    }
  }
  ```
- **Estimated saving:** 0 frame ms; **500 ms–2 s** off worst-case tap latency (ESTIMATED). Session resumption is the highest-value item here and is a three-line change.
- **Accuracy impact:** n/a. `setInsecure()` means there is no certificate validation to weaken; session reuse does not change the (already absent) trust model.
- **Risk:** the deferred-action flag must **not** redraw from inside the poll callback — the PNG decoder is at that moment using the frame sprite's pixels as its scratch (`main.cpp:151-152`, `platform_png::setScratch(radarDisplayFrameScratch)`; see `terrain.cpp:218-223`). Drawing into the sprite mid-decode would corrupt the decode, and the existing code is careful about exactly this (`maybeFetchTerrain` repaints only on the edge where a download **stops**). Any change must keep the rule "no drawing while a decode is in flight" — hence a flag rather than a `radarDisplayDraw()` call. Session reuse costs a persistent `sslsession` (a few hundred bytes plus the ticket) from the ~45 KB heap.
- **Verification:** log `esp_timer_get_time()` deltas around `http.GET()` for the first request and for subsequent ones — resumption shows as a large drop on the second. For tap latency, timestamp in `onBootButtonIsr` and again in `onRangeTap` and log the delta, driven by tapping during a terrain download.

---

## Things checked and found clean

- **`src/ui/terrain_overlay.cpp` and `src/platform/png_decode.cpp` contain exactly zero soft-float call sites.** DISASSEMBLY-CONFIRMED by scanning relocations in every object under `.pio/build/supermini/src/`. The fixed-point rewrite is complete; PERF-01 is about call overhead and a linear scan, not float. Full census:

  | object | soft-float + libm relocation sites |
  |---|---|
  | `ui/runway_overlay.cpp.o` | 109 |
  | `core/terrain.cpp.o` | 83 |
  | `ui/radar_display.cpp.o` | 58 |
  | `core/geo.cpp.o` | 41 |
  | `ui/status_screens.cpp.o` | 22 |
  | `core/adsb.cpp.o` | 15 |
  | `core/settings.cpp.o` | 11 |
  | `main.cpp.o` | 3 |
  | `ui/terrain_overlay.cpp.o`, `platform/png_decode.cpp.o`, all of `platform/device/`, `core/tap_gesture.cpp.o`, `core/large_airports_data.cpp.o`, `core/airport_find.cpp.o` | **0** |

- **`LGFXBase::drawCircle` is pure integer Bresenham** (`LGFXBase.cpp:230-261`) — no float. The rings are ~3.5 ms, not a hidden float loop.
- **`core::adsb`'s float work is all at fetch time**, once per 10 s (`readJsonFloat`, `pickNoseHeading`, `buildUrl`). `Aircraft` already stores `float` lat/lon, so nothing per-frame is widened there.
- **`formatRing3Label` uses `%d`, not `%f`** — no `double` formatting in `snprintf` on the frame path. One `lroundf` + one `__divsf3` per frame; folded into PERF-07 only because caching is free.
- **VLW font metric tables are cached in heap** at `loadFont` (`lgfx_fonts.cpp:1843-1856` allocates `gWidth`/`gxAdvance`/`gdX`), so `updateFontMetric` is an array read, not a blob re-read. `displayFontEnsureLoaded` short-circuits when already loaded (`display_font.cpp:32-40`).
- **`initLabelMetrics` / `initTagLabelMetrics` / `initRunwayLabelStyle` are already once-only**, including the 16-step binary search in `findVlwSizeForHeight` and the 8-way scale-label width scan. Not per-frame.
- **Taps are never dropped** — `onBootButtonIsr` latches on `CHANGE` under `portENTER_CRITICAL_ISR`; a tap during a 2 s block is late, not lost.
- **`drawAircraft`'s stack arrays** (`AircraftDrawItem items[64]`, `BeyondDotDrawItem dots[64]`) have NSDMIs, so the compiler emits `memset`/`memcpy` to zero ~1.8 KB per frame (DISASSEMBLY-CONFIRMED: 2×`memset`, 4×`memcpy`). ~0.02 ms — measured and dismissed.
- **The insertion sorts** (`sortDrawItemsFarFirst`, `sortBeyondDotsFarFirst`) are O(n²) over ≤64 items, ~2,000 integer comparisons worst case. Negligible.

## Suggested order of work

1. **PERF-03** (crosshairs → rect fills). ~20 ms, ~20 lines, pixel-identical, touches nothing else. Best ratio here.
2. **PERF-02** (cache the airport in-range set). ~19 ms, and it *frees* 2 KB of heap. Needs the invalidation key done carefully.
3. **PERF-01 step 1** (local `bandOf`). ~20 ms, ~15 lines, exact output.
4. **PERF-04** (`Viewport` → `float`). ~2 ms, simplifies 2 and 6. Widens `test_geo` tolerances.
5. **PERF-11 (a)** (TLS session reuse). Three lines, 0.5–2 s off tap latency — arguably the largest *user-visible* win here, and not a frame optimization at all.
6. **PERF-09** (integer `terrariumElevation`). Two lines, covered by existing tests.
7. **PERF-06**, **PERF-07**, **PERF-05**, **PERF-01 step 2**, **PERF-08**.

Before any of it: land a per-stage timer, because every millisecond figure above is derived from instruction counts and a single calibration point.

```cpp
// radar_display.cpp, behind a build flag
#if defined(PLANE_RADAR_PROFILE)
  #define PROF(name, expr) do { const int64_t t0 = esp_timer_get_time(); expr; \
    core::platform::logf("prof %-14s %6lld us\n", name, esp_timer_get_time() - t0); } while (0)
#else
  #define PROF(name, expr) expr
#endif
…
PROF("fillScreen",  gfx.fillScreen(radar::kColorBackground));
PROF("terrain",     terrain::drawTerrainBackground(gfx));
PROF("rings",       drawRings(cx, cy, grid_r));
PROF("crosshairs",  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid));
PROF("runways",     runway::drawLargeAirportRunways(gfx));
PROF("labels",      (drawCardinalLabels(), drawScaleLabel(cx, cy, grid_r), drawSiteLabel(cx, cy, grid_r)));
PROF("aircraft",    drawAircraft());
PROF("blit",        s_frame.pushSprite(0, 0));
```

---

**Notes on what was run:** `pio run -e supermini` (clean SUCCESS, RAM 17.9 % / Flash 40.2 %), one experimental build with `PLATFORMIO_BUILD_SRC_FLAGS="-O2 -std=gnu++17"` (env var only — `platformio.ini` untouched), then a rebuild back to the default which reproduced byte-identical sizes. No source file, `.ini`, `Makefile`, git state, or hardware was touched, and no upload was attempted.
