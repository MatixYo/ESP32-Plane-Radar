# Findings 04 — Target architecture: pure core, imperative shell

Design-only. Nothing in `src/`, `include/`, `test/`, `scripts/`, `*.ini` or `Makefile` was modified.

## Baseline measured before designing

| Fact | Value | How measured |
|---|---|---|
| Toolchain | `riscv32-esp-elf-g++ (crosstool-NG esp-2021r2-patch5) 8.4.0` | `--version` |
| Our sources compiled at | `-std=gnu++17` | `platformio.ini` `[env:supermini]` |
| Framework flags in effect | `-fno-rtti`, **`-fexceptions`** | `compile_commands.json` |
| Device build | SUCCESS, RAM 58,668 B (17.9 %), flash 1,264,250 B (40.2 %) | `make build` |
| Host tests | 112 passing | `make test` |

Counts: `test_adsb` 10, `test_geo` 15, `test_gesture` 3, `test_settings` 17, `test_terrain` 27 (= 72 in `native_test`), `test_terrain_fetch` 14, `test_png` 26 → **112**. `test_terrain_live` (3) is opt-in.

> **Correction to the brief.** RTTI is off, but exceptions are **on** (`-fexceptions`). Nothing throws or catches; `include/core/platform.h:119` records that the reason to avoid big allocations is that `std::bad_alloc` would abort the firmware. Designs below never rely on exceptions — but "`std::variant` won't compile" would be a wrong claim. I argue against it on size/inspectability grounds (§8).

Every C++ snippet in §2, §4 and §6 was compiled with the real `riscv32-esp-elf-g++ 8.4.0` at `-std=gnu++17 -fno-rtti -Os -Wall -Wextra`, clean, with the stated `sizeof`s pinned by `static_assert`. Where I am unsure, it says so.

---

## 1. Current state map

Sizes are the linker's, from `riscv32-esp-elf-gcc-nm --print-size -C .pio/build/supermini/firmware.elf`. Lifetime: **process** = never reset; **session** = reset on WiFi up/down; **download**/**tile** = valid only mid-fetch; **frame** = valid inside one composition; **boot-once** = written by a lazy `init*()` guard and never again.

### 1.1 `src/main.cpp` — the only declared globals (anonymous namespace)

| file:line | name | type | bytes | read by | written by | lifetime |
|---|---|---|---:|---|---|---|
| main.cpp:26 | `g_radar_visible` | `bool` | 1 | :48, 64, 103, 164, 184 | :34, 38, 167 | session |
| main.cpp:27 | `g_wifi_down_since` | `unsigned long` | 4 | :170, 173 | :171, 178, 183 | session |
| main.cpp:28 | `g_last_reconnect_ms` | `unsigned long` | 4 | :175 | :176 | session |
| main.cpp:29 | `g_last_adsb_fetch_ms` | `unsigned long` | 4 | :186 | :67, 188 | session |
| main.cpp:30 | `g_terrain_download_active` | `bool` | 1 | :119 | :122 | download |
| | **subtotal** | | **14** | | | |

`g_terrain_download_active` is the borrowed-scratch invariant's entire current representation: main.cpp:119-122 repaints on the falling edge of `core::terrain::downloadActive()`. See §4.

### 1.2 `src/core/settings.cpp` — anonymous namespace

| file:line | name | type | bytes | read by | written by | lifetime |
|---|---|---|---:|---|---|---|
| settings.cpp:32 | `s_lat` | `double` | 8 | `lat()` :168 → radar_display.cpp:211, terrain_overlay.cpp:120, runway_overlay.cpp:78, portal_params.cpp:73, main.cpp:61,106,127 | :151, 185, 198, 276; `applyActiveSiteCoords()` :83 | process |
| settings.cpp:33 | `s_lon` | `double` | 8 | as above | as above | process |
| settings.cpp:34 | `s_range_index` | `uint8_t` | 1 | :299, :301 | :157, 295 | process |
| settings.cpp:35 | `s_use_km` | `bool` | 1 | :305, 381 | :158, 312, 330 | process |
| settings.cpp:36 | `s_show_runways` | `bool` | 1 | :307 | :159, 318, 331 | process |
| settings.cpp:37 | `s_show_terrain` | `bool` | 1 | :309 | :160, 324, 332 | process |
| settings.cpp:39 | `s_site_idents` | `char[6][5]` | 30 | :63-64, 80, 215, 222, 229 | :124, 203, 266, 268 | process |
| settings.cpp:40 | `s_site_count` | `size_t` | 4 | :52, 59, 75-76, 184, 212, 226, 235, 262-273 | :87, 128, 200, 262 | process |
| settings.cpp:41 | `s_site_index` | `uint8_t` | 1 | :76, 80, 229, 232, 238 | :76, 88, 201, 238, 263 | process |
| | **subtotal** | | **55** | | | |

### 1.3 `src/core/adsb.cpp` — anonymous namespace

| file:line | name | type | bytes | read by | written by | lifetime |
|---|---|---|---:|---|---|---|
| adsb.cpp:19 | `s_aircraft` | `Aircraft[64]` | 3072 | `aircraftList()` :270 → radar_display.cpp:420 | `parseBody()` :236-241 | session |
| adsb.cpp:20 | `s_aircraft_count` | `size_t` | 4 | :268, 292 | :210, 258, 266 | session |
| adsb.cpp:21 | `s_poll_fn` | `PollFn` | 4 | :287 | :264 ← main.cpp:149 | process |
| | **subtotal** | | **3080** | | | |

### 1.4 `src/core/terrain.cpp` — anonymous namespace

| file:line | name | type | bytes | read by | written by | lifetime |
|---|---|---|---:|---|---|---|
| terrain.cpp:38 | `s_grid` | `Grid` | 3392 | `grid()` :256, `gridReady()` :247 → terrain_overlay.cpp:123-126 | :240, 396, 411-413, 474-477; via `s_tile_grid` :208 | process (1 slot) |
| terrain.cpp:39 | `s_grid_range_index` | `uint8_t` | 1 | :247, 253 | :241, 412, 478 | process |
| terrain.cpp:45 | `s_fail_ms` | `unsigned long` | 4 | :392 | :227 | process |
| terrain.cpp:46 | `s_fail_range_index` | `uint8_t` | 1 | :391 | :228, 242, 479 | process |
| terrain.cpp:48 | `s_poll_fn` | `PollFn` | 4 | :439 | :233 ← main.cpp:150 | process |
| terrain.cpp:49 | `s_png_decode` | `PngDecodeFn` | 4 | :215, 388 | :235 ← main.cpp:151 | process |
| terrain.cpp:72 | `s_prog` | `Progress` | 104 | :237, 397, 426-439, 456-482 | :223, 399-422, 446, 456-458 | download |
| terrain.cpp:83 | `s_col_px` | `int32_t[41]` | 164 | :162 | :140 | download |
| terrain.cpp:84 | `s_row_py` | `int32_t[41]` | 164 | :171 | :141 | download |
| terrain.cpp:92 | `s_col_local` | `int16_t[41]` | 82 | :167, 206 | :166 | tile |
| terrain.cpp:93 | `s_row_local` | `int16_t[41]` | 82 | :174, 204 | :173 | tile |
| terrain.cpp:105 | `s_row_first` | `int16_t[256]` | 512 | :192 | :149, 175 | tile |
| terrain.cpp:106 | `s_col_first` | `int16_t[256]` | 512 | :196 | :150, 168 | tile |
| terrain.cpp:109 | `s_tile_grid` | `Grid*` | 4 | :188, 208 | :147, 440 | tile |
| terrain.cpp:110 | `s_tile_filled` | `int` | 4 | :457 | :148, 209 | tile |
| | **subtotal** | | **5034** | | | |

`s_tile_grid`/`s_tile_filled` exist only because `PixelFn` (terrain.h:58) is captureless **and** the `ctx` parameter it already carries is passed as `nullptr` (terrain.cpp:215). Fixable — §7 Stage 3.

### 1.5 `src/core/tap_gesture.cpp` / `src/core/portal_params.cpp`

| file:line | name | type | bytes | read by | written by | lifetime |
|---|---|---|---:|---|---|---|
| tap_gesture.cpp:9 | `s_first_tap_ms` | `unsigned long` | 4 | :17, 35 | :23, 44 | session |
| tap_gesture.cpp:10 | `s_have_pending` | `bool` | 1 | :16, 34 | :18, 24, 36, 45 | session |
| tap_gesture.cpp:11 | `s_ready` | `Tap` | 4 | :28, 29 | :19, 30, 46 | session |
| portal_params.cpp:37 | `s_pending_lat` | `char[21]` | 21 | :149 | :110, 154 | one form POST |
| portal_params.cpp:38 | `s_pending_lon` | `char[21]` | 21 | :149 | :114, 155 | one form POST |
| portal_params.cpp:39 | `s_pending_sites` | `char[6][6]` | 36 | :143-144 | :104, 157 | one form POST |
| | **subtotal** | | **87** | | | |

`s_ready` is 4 bytes for a 3-value enum: `enum class Tap` with no fixed underlying type defaults to `int`.

### 1.6 `src/ui/radar_display.cpp`

| file:line | name | type | bytes | read by | written by | lifetime |
|---|---|---|---:|---|---|---|
| radar_theme.h:137-147 / radar_display.cpp:24-34 | `ui::radar::kColor*` ×10 + `kColorTerrain[7]` | `uint16_t` **mutable, external linkage** | 34 | everywhere in ui/ | `initPalette()` :179-206, called from :620, 658, 674 — three times per frame | process |
| radar_display.cpp:40 | `s_label_metrics_ready` | `bool` | 1 | :114 | :157 | boot-once |
| radar_display.cpp:41 | `s_cardinal_use_vlw` | `bool` | 1 | :485 | :121, 132 | boot-once |
| radar_display.cpp:42 | `s_scale_use_vlw` | `bool` | 1 | :493 | :125, 139 | boot-once |
| radar_display.cpp:43 | `s_cardinal_vlw_size` | `float` | 4 | :486 | :122 | boot-once |
| radar_display.cpp:44 | `s_scale_vlw_size` | `float` | 4 | :494 | :126 | boot-once |
| radar_display.cpp:45 | `s_tag_vlw_size` | `float` | 4 | :299 | :168 | boot-once |
| radar_display.cpp:46 | `s_cardinal_gfx` | `const GFXfont*` | 4 | :487 | :130 | boot-once |
| radar_display.cpp:47 | `s_scale_gfx` | `const GFXfont*` | 4 | :495 | :138 | boot-once |
| radar_display.cpp:48 | `s_tag_gfx` | `const GFXfont*` | 4 | :301 | :172 | boot-once |
| radar_display.cpp:50 | `s_tag_label_metrics_ready` | `bool` | 1 | :161 | :176 | boot-once |
| radar_display.cpp:51 | `s_tag_use_vlw` | `bool` | 1 | :298 | :167, 173 | boot-once |
| radar_display.cpp:53 | `s_scale_label_max_w` | `int` | 4 | :151 (own loop only) | :144, 152 | **DEAD** |
| radar_display.cpp:54 | `s_scale_label_h` | `int` | 4 | never | :143 | **DEAD** (elided; not in the ELF) |
| radar_display.cpp:56 | `s_draw` | `LovyanGFX*` | 4 | :221, 269, 293, 299-354, 502-596 | `DrawScope` :62-63 | frame |
| radar_display.cpp:57 | `s_frame` | `LGFX_Sprite` | 344 handle + **115,200 heap** | :646-651, 693 | :633-638 | process |
| radar_display.cpp:58 | `s_frame_ready` | `bool` | 1 | :630 | :638 | process |
| | **subtotal** | | **420** (+115,200 heap) | | | |

### 1.7 Remaining `src/ui/`

| file:line | name | type | bytes | read by | written by | lifetime |
|---|---|---|---:|---|---|---|
| runway_overlay.cpp:20 | `s_in_range` | `bool[1166]` | 1166 | :275, 284 | :268, 282 | frame |
| runway_overlay.cpp:21 | `s_label_pending` | `bool[1166]` | 1166 | :290 | :269, 291 | frame |
| runway_overlay.cpp:23 | `s_runway_label_ready` | `bool` | 1 | :48 | :60 | boot-once |
| runway_overlay.cpp:24 | `s_runway_label_use_vlw` | `bool` | 1 | :64 | :54, 58 | boot-once |
| runway_overlay.cpp:25 | `s_runway_label_vlw_size` | `float` | 4 | :65 | :55 | boot-once |
| runway_overlay.cpp:26 | `s_runway_label_gfx` | `const GFXfont*` | 4 | :67 | :57 | boot-once |
| terrain_overlay.cpp:39 | `s_cell` | `int[240]` | 960 | :65, 80 | :58 | boot-once |
| terrain_overlay.cpp:40 | `s_frac` | `int32_t[240]` | 960 | :68, 81 | :59 | boot-once |
| terrain_overlay.cpp:41 | `s_map_ready` | `bool` | 1 | :44 | :61 | boot-once |
| status_screens.cpp:33 | `s_connecting_ssid` | `char[33]` | 33 | :111, 117, 120 | :185 | connect screen |
| status_screens.cpp:34 | `s_ssid_line` | `char[33]` | 33 | :114, 121, 146 | :111, 119, 125 | connect screen |
| status_screens.cpp:36 | `s_spinner_angle_deg` | `float` | 4 | :164, 203 | :188, 202, 204 | connect screen |
| status_screens.cpp:37 | `s_spinner_dots` | `SpinnerDot[10]` | 120 | :153-157 | :175-177, 190 | connect screen |
| status_screens.cpp:38 | `s_connecting_text_drawn` | `bool` | 1 | :198 | :148, 192 | connect screen |
| display_font.cpp:8 | `s_vlw_loaded` | `bool` | 1 | :24, 30, 33 | :22 | boot-once |
| | **subtotal** | | **4455** | | | |

`s_cell`/`s_frac` (1,920 B of `.bss`) are a pure function of `ui::radar::kSize` and `core::terrain::kGridSize`. Nothing at run time can change them. §2.4 moves them to flash.

### 1.8 `src/platform/`

| file:line | name | type | bytes | notes | lifetime |
|---|---|---|---:|---|---|
| png_decode.cpp:56 | `s_scratch_fn` | `ScratchFn` | 4 | wired at main.cpp:152; §4 deletes it | process |
| wifi_setup_device.cpp:23 | `s_boot_mux` | `portMUX_TYPE` | 4 | **external linkage** (no namespace) | process |
| wifi_setup_device.cpp:24 | `s_boot_tap_pending` | `volatile bool` | 1 | **external linkage**; ISR :41 | ISR ↔ loop |
| wifi_setup_device.cpp:25 | `s_boot_is_down` | `volatile bool` | 1 | **external linkage**; ISR | ISR ↔ loop |
| wifi_setup_device.cpp:26 | `s_boot_down_ms` | `volatile unsigned long` | 4 | **external linkage**; ISR | ISR ↔ loop |
| wifi_setup_device.cpp:27 | `s_long_press_handled` | `bool` | 1 | **external linkage** | press |
| wifi_setup_device.cpp:28 | `s_boot_interrupt_attached` | `bool` | 1 | **external linkage** | process |
| wifi_setup_device.cpp:63 | `s_force_config_portal` | `bool` | 1 | | process |
| wifi_setup_device.cpp:64 | `s_wm` | `WiFiManager` | 620 | resource, not state | process |
| wifi_setup_device.cpp:65 | `s_wm_configured` | `bool` | 1 | | process |
| wifi_setup_device.cpp:80 | `s_params` | `WiFiManagerParameter*[12]` | 48 | **`new`** at :119 — the only post-boot heap allocation we make | process |
| wifi_setup_device.cpp:86 | `s_attrs` | `char[12][64]` | 768 | must outlive the portal (pointer retained) | process |
| wifi_setup_device.cpp:87 | `s_param_count` | `size_t` | 4 | | process |
| display_device.cpp:7 / display_native.cpp:19 | `tft` | `LGFX` | 528 | **external linkage**, by design (display.h:18) | process |
| button_sdl.cpp:32-36 | `s_key_mapped`, `s_boot_is_down`, `s_boot_down_ms`, `s_boot_tap_pending`, `s_long_press_handled` | — | 11 | native only | process/press |
| wifi_setup_native.cpp:51,52,62 | `s_link_up`, `s_force_config_portal`, `s_in_wifi_loop` | `bool` ×3 | 3 | native only | session |
| portal_server.cpp:66-68 | `s_listen_fd`, `s_credentials_pending`, `s_pending_ssid` | `int`,`bool`,`std::string` | ~40 | native only; `std::string` heap-allocates | process |
| kv_json_file.cpp:115 | `loaded` | function-local `static JsonDocument` | heap | native only | process |
| font_blob_file.cpp:27 | `data` | function-local `static std::vector<uint8_t>` | heap | native only; pointer retained by `loadFont()` | process |
| | **device subtotal** | | **1,986** (incl. `tft`) | | |

### 1.9 Totals

| Group | bytes |
|---|---:|
| `main.cpp` globals | 14 |
| `core/` statics | 8,256 |
| `ui/` statics (incl. sprite *handle*, excl. its 115,200 heap bytes) | 4,875 |
| `platform/` device statics (incl. `tft`) | 1,986 |
| **Our own static mutable state, device build** | **15,127** |
| of which are pure derived caches / per-frame workspace | 6,391 |
| of which are resource handles (`tft` 528, `s_frame` 344, `s_wm` 620) | 1,492 |
| of which is genuine application state | **7,244** |
| Heap held for the frame sprite | 115,200 |

That 7,244 / 6,391 split is the whole thesis of §2: only the first number belongs in `AppState`.

### 1.10 Two defects found while mapping

1. `s_scale_label_h` (radar_display.cpp:54) is written and never read; `s_scale_label_max_w` (:53) is read only inside the loop that computes it. Both dead. 8 bytes, one already gone from the ELF.
2. `s_boot_mux`, `s_boot_tap_pending`, `s_boot_is_down`, `s_boot_down_ms`, `s_long_press_handled`, `s_boot_interrupt_attached` (wifi_setup_device.cpp:23-28) and `onBootButtonIsr`/`initBootButton` (:30, :47) sit at **global scope with external linkage** — `nm` reports capital `B`. They are `s_`-prefixed as if file-local but are not; `button_sdl.cpp` defines same-named symbols with *internal* linkage, so the two destinations differ in a way nothing checks. Wrapping them in the file's existing anonymous namespace is a one-line, zero-risk fix for Stage 0.

---

## 2. Proposed `AppState`

### 2.1 Shape

```cpp
// include/app/state.h — no Arduino, no LovyanGFX, no ESP-IDF. Host-testable.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/geo.h"
#include "core/settings_model.h"
#include "core/terrain_model.h"
#include "core/traffic_model.h"
#include "core/gesture_model.h"

namespace app {

using Millis = unsigned long;

/**
 * Which side of the borrowed-scratch invariant currently owns the frame
 * sprite's 115,200 pixel bytes. See §4 — this enum IS the invariant.
 */
enum class FramePhase : uint8_t {
  kIdle,          ///< panel holds the last blit; the pixels are free to lend
  kComposing,     ///< a ui::FrameLease is live; the compositor owns them
  kDecodingTile,  ///< a ui::ScratchLease is live; the PNG decoder owns them
};

struct FrameState {
  FramePhase phase = FramePhase::kIdle;
  bool dirty = false;      ///< a repaint is owed (was: main.cpp's falling edge)
  bool available = false;  ///< the sprite exists (was: s_frame_ready)

  bool canLend() const { return available && phase == FramePhase::kIdle; }
};

/** Everything that resets when the link comes and goes. Was main.cpp:26-29. */
struct Session {
  bool radarVisible = false;
  Millis wifiDownSince = 0;
  Millis lastReconnectMs = 0;
  Millis lastTrafficFetchMs = 0;
};

struct AppState {
  core::settings::Settings settings;
  core::traffic::TrafficStore traffic;
  core::terrain::TerrainGrid terrain;
  core::terrain::TileDownload download;
  core::terrain::RetryGate retry;
  core::gesture::TapRecognizer tap;
  Session session;
  FrameState frame;

  AppState() = default;
  AppState(const AppState&) = delete;             // §8: no per-action copies
  AppState& operator=(const AppState&) = delete;

  // --- const queries only. Anything that mutates is a free function. --------
  core::geo::GeoPoint center() const { return settings.center; }
  bool terrainMatchesView() const {
    return terrain.covers(settings.center, settings.rangeIndex);
  }
  bool downloadInFlight() const { return download.active; }
};

}  // namespace app
```

### 2.2 The sub-structs

```cpp
// include/core/geo.h
namespace core::geo {

constexpr float kKmPerDeg = 111.0f;   // unchanged: geo.h:15

struct GeoPoint  { double lat = 0.0; double lon = 0.0; };
struct ScreenPoint { int x = 0; int y = 0; };
struct GroundOffset { float dx_km = 0.0f; float dy_km = 0.0f; float dist_km = 0.0f; };

/**
 * Was core::geo::Viewport (geo.h:18). Now carries const query methods that own
 * the projection; the free functions of geo.cpp become their bodies verbatim.
 * int16_t for the pixel fields: the panel is 240 px, and this makes ViewSpan
 * 32 bytes instead of 40 — it is built once per frame.
 */
struct ViewSpan {
  GeoPoint center;
  int16_t center_x = 0;
  int16_t center_y = 0;
  int16_t outer_radius_px = 0;
  float outer_km = 0.0f;

  GroundOffset offset(float lat, float lon) const;
  ScreenPoint project(float lat, float lon) const;
  float innerRingMaxKm(int inset_px) const;
  bool containsKm(float dist_km, int inset_px) const;
  int distSq(ScreenPoint p) const;
  std::optional<ScreenPoint> rimPoint(float lat, float lon, int inset_px,
                                      int rim_radius_px) const;
  ScreenPoint clipToOuterRing(int max_r, ScreenPoint from, ScreenPoint to) const;
  bool segmentIntersectsDisc(ScreenPoint a, ScreenPoint b) const;
};

}  // namespace core::geo
```

```cpp
// include/core/settings_model.h
namespace core::settings {

constexpr size_t kMaxSites = 6;

struct RangePreset { float ring3_km; float outer_km; };  // unchanged

struct SiteList {
  char idents[kMaxSites][5] = {};
  uint8_t count = 0;
  uint8_t index = 0;

  std::string_view activeIdent() const {
    return count == 0 ? std::string_view{} : std::string_view(idents[index]);
  }
  std::string_view identAt(size_t slot) const {
    return slot >= count ? std::string_view{} : std::string_view(idents[slot]);
  }
  bool isCyclable() const { return count >= 2; }
  uint8_t nextIndex() const {
    return count < 2 ? index : static_cast<uint8_t>((index + 1) % count);
  }
};

struct Settings {
  core::geo::GeoPoint center;   ///< zero-initialised; boot fills the default.
                                ///< See §2.3 — a non-zero default here costs
                                ///< 6.6 KB of flash by moving AppState to .data.
  SiteList sites;
  uint8_t rangeIndex = 0;
  bool useKm = false;
  bool showRunways = false;
  bool showTerrain = false;

  const RangePreset& range() const;
  float outerKm() const { return range().outer_km; }
  float ring3Km() const { return range().ring3_km; }
};

}  // namespace core::settings
```

```cpp
// include/core/traffic_model.h  (was core/aircraft.h)
namespace core::traffic {

constexpr size_t kMaxAircraft = 64;

/** Was core::adsb::Aircraft (aircraft.h:13). Byte-identical layout. */
struct AircraftTarget {
  float lat = 0.0f;
  float lon = 0.0f;
  float nose_deg = 0.0f;
  float track_deg = 0.0f;
  float gs_knots = 0.0f;
  char callsign[9] = {};
  char type[5] = {};
  char alt[12] = {};
};

struct TrafficStore {
  AircraftTarget targets[kMaxAircraft] = {};
  uint8_t count = 0;                     ///< was size_t; 64 fits in uint8_t

  const AircraftTarget* begin() const { return targets; }
  const AircraftTarget* end() const { return targets + count; }
  bool isEmpty() const { return count == 0; }
};

/** Screen-space plot of one target: the pure product ViewSpan x AircraftTarget. */
struct AircraftPlot {
  uint8_t index = 0;
  int16_t x = 0;
  int16_t y = 0;
  int32_t dist_sq = 0;
};

}  // namespace core::traffic
```

```cpp
// include/core/terrain_model.h
namespace core::terrain {

constexpr int kGridSize = config::kTerrainGridSize;   // 41
constexpr int kGridPoints = kGridSize * kGridSize;    // 1681
constexpr int kTilePx = 256;
constexpr int kMaxTiles = 4;

struct TileId { int16_t z = 0; int16_t x = 0; int16_t y = 0; };  // was 3x int

/** Was core::terrain::Grid (terrain.h:42), plus the range key that used to be
 *  the separate s_grid_range_index. NEVER copy: 3,392 bytes. */
struct TerrainGrid {
  bool valid = false;
  uint8_t rangeIndex = 0;
  core::geo::GeoPoint center;
  float half_span_km = 0.0f;
  int16_t elev_m[kGridPoints] = {};

  TerrainGrid() = default;
  TerrainGrid(const TerrainGrid&) = delete;            // 3.4 KB copy: forbid it
  TerrainGrid& operator=(const TerrainGrid&) = delete;

  int16_t elevation(int row, int col) const {
    return elev_m[row * kGridSize + col];
  }
  /** Row 0 = north edge, col 0 = west edge. Was terrain.cpp:246-250. */
  bool covers(core::geo::GeoPoint c, uint8_t range) const;
};

/** Was terrain.cpp:55-68's anonymous Progress + the persistent axis tables. */
struct TileDownload {
  bool active = false;
  uint8_t rangeIndex = 0;
  uint8_t tileCount = 0;
  uint8_t nextTile = 0;
  uint8_t failures = 0;
  core::geo::GeoPoint center;
  float half_span_km = 0.0f;
  int16_t zoom = 0;
  uint16_t filled = 0;
  TileId tiles[kMaxTiles];
  unsigned long lastRequestMs = 0;
  /// Global Mercator pixel axes of the grid's samples. Survives ACROSS tiles,
  /// so unlike the per-tile tables it cannot live in the borrowed scratch.
  int32_t colPx[kGridSize] = {};
  int32_t rowPy[kGridSize] = {};

  bool matches(core::geo::GeoPoint c, uint8_t range, float span) const;
};

/** Was s_fail_ms / s_fail_range_index (terrain.cpp:45-46). */
struct RetryGate {
  unsigned long sinceMs = 0;
  uint8_t rangeIndex = 0;
  bool armed = false;   ///< explicit flag, not a 0xFF sentinel — see §2.3
  bool blocks(uint8_t range, unsigned long now, unsigned long window) const {
    return armed && rangeIndex == range && now - sinceMs < window;
  }
};

}  // namespace core::terrain
```

```cpp
// include/core/gesture_model.h
namespace core::gesture {

enum class TapKind : uint8_t { kNone, kSingle, kDouble };  // was 4 bytes

struct TapRecognizer {
  unsigned long firstTapMs = 0;
  bool hasPending = false;
  TapKind ready = TapKind::kNone;
};

}  // namespace core::gesture
```

### 2.3 Size, verified

Compiled with `riscv32-esp-elf-g++ 8.4.0 -std=gnu++17 -fno-rtti -Os -Wall -Wextra`, clean, sizes pinned by `static_assert`:

| Type | bytes | replaces (bytes today) |
|---|---:|---|
| `core::geo::ViewSpan` | 32 | `Viewport`, built per frame on the stack (40) |
| `core::settings::SiteList` | 32 | `s_site_idents`+`s_site_count`+`s_site_index` (35) |
| `core::settings::Settings` | 56 | settings.cpp's 9 statics (55) |
| `core::traffic::AircraftTarget` | 48 | `Aircraft` (48) |
| `core::traffic::TrafficStore` | 3,076 | `s_aircraft`+`s_aircraft_count` (3,076) |
| `core::traffic::AircraftPlot` | 12 | `AircraftDrawItem` (16) |
| `core::terrain::TerrainGrid` | 3,392 | `s_grid`+`s_grid_range_index` (3,393) |
| `core::terrain::TileDownload` | 392 | `s_prog`+`s_col_px`+`s_row_py` (432) |
| `core::terrain::RetryGate` | 8 | `s_fail_ms`+`s_fail_range_index` (5) |
| `core::gesture::TapRecognizer` | 8 | tap_gesture.cpp's 3 statics (9) |
| `app::Session` | 16 | main.cpp:26-29 (13) |
| `app::FrameState` | 3 | `s_frame_ready`+`g_terrain_download_active` (2) |
| **`app::AppState`** | **6,960** | — |

`AppState` without the axis tables measured **6,632 bytes exactly** (`nm`: `00000000 000019e8 B appState`); adding `colPx`/`rowPy` (328 B) brings it to 6,960.

**Verified finding that changes the design.** With non-zero default member initialisers (`rangeIndex = 1`, `showRunways = true`, `center{47.0753, 15.4062}`, `0xFF` sentinels), `AppState` lands in **`.data`**, not `.bss`:

```
$ riscv32-esp-elf-size -A proto.o        # non-zero defaults
.data   13264      .bss   0
$ riscv32-esp-elf-size -A bss2.o         # all-zero defaults
.data   0          .bss   6632
```

`.data` costs the RAM *and* an equal-sized initialiser image in flash. So **every field of `AppState` must default to zero**, with real defaults applied at boot by `loadSettings()` — where they belong anyway, since they already have to be merged with NVS. That is why `rangeIndex` defaults to `0` above and `RetryGate` grew an explicit `armed` flag instead of a `0xFF` sentinel. No `static_assert` can guard this; the guard is a CI grep for non-zero literals in the model headers.

### 2.4 RAM budget: target vs today

| Item | today | target | note |
|---|---:|---:|---|
| `AppState` | — | 6,960 | `.bss` |
| Application state today (settings/traffic/terrain/gesture/session) | 7,244 | — | replaced above |
| Terrain pixel→grid map | 1,920 (`.bss`) | **0** | `constexpr TerrainPixelMap` → 960 B `.rodata` (flash). Verified: `00000000 000003c0 r ui::terrain::kPixelMap` |
| Runway per-airport memo | 2,332 | **296** | two `bool[1166]` → two `uint32_t[37]` bitsets |
| Per-tile resampler tables (`s_col_local`, `s_row_local`, `s_row_first`, `s_col_first`) | 1,188 | **0** | tile-scoped ⇒ live in the borrowed scratch's tail (§4.4) |
| Label metrics / font styles | 34 | 32 | `LabelMetrics` in the render cache |
| Palette | 34 | 24 | `Palette` struct, packed |
| Connect-screen animation | 191 | 191 | unchanged, just named and scoped |
| Dead statics | 8 | 0 | §1.10 |
| Resource handles (`tft`, sprite, `WiFiManager`, portal buffers) | 1,986 | 1,986 | untouched |
| **Total** | **15,127** | **9,489** | **−5,638 B of static RAM** |

Not cosmetic. Every byte of `.bss` given back is a byte the heap can hand to a TLS session, and the reason this firmware has an allocation-free PNG decoder at all is that the three consumers overrun the heap by ~13 KB (png_decode.h:6-14). 5.6 KB is ~43 % of that deficit.

---

## 3. Pure / effectful partition

`PURE` = deterministic function of its arguments, no I/O, no clock, no globals, host-testable with no fakes. `EFFECTFUL` = touches display, NVS, network, clock, or mutates `AppState`. Per the user's rule, **mutation is always a free function taking `AppState&` and always lives in the shell** — no mutating member functions anywhere.

### 3.1 `src/core/geo.cpp` — all pure today, becomes const query methods

| today | verdict | proposed |
|---|---|---|
| `offsetKmFromCenter(const Viewport&, float, float)` :7 | PURE | `GroundOffset ViewSpan::offset(float, float) const` |
| `latLonToScreen(const Viewport&, float, float)` :15 | PURE | `ScreenPoint ViewSpan::project(float, float) const` |
| `innerRingMaxKm(const Viewport&, int)` :26 | PURE | `float ViewSpan::innerRingMaxKm(int) const` |
| `isInsideOuterRingKm(const Viewport&, float, int)` :31 | PURE | `bool ViewSpan::containsKm(float, int) const` |
| `distSqFromCenter(const Viewport&, int, int)` :35 | PURE | `int ViewSpan::distSq(ScreenPoint) const` |
| `rimPointForDistantTarget(..., Point*)` :41 | PURE | `std::optional<ScreenPoint> ViewSpan::rimPoint(float, float, int, int) const` |
| `clipPointToOuterRing(..., int*, int*)` :59 | PURE | `ScreenPoint ViewSpan::clipToOuterRing(int, ScreenPoint, ScreenPoint) const` |

The two out-parameter signatures become value returns: `ScreenPoint` is 8 bytes, `std::optional<ScreenPoint>` 12 — both register/stack, no allocation, and `optional` needs no exceptions as long as we never call `.value()`.

### 3.2 `src/core/settings.cpp`

| today | verdict | proposed |
|---|---|---|
| `parseCoord(const char*, double*)` :341 | PURE | `std::optional<double> parseCoord(std::string_view)` |
| `validLatLon(double, double)` :354 | PURE | `bool GeoPoint::isValid() const` |
| `portalCheckboxChecked(const char*)` :358 | PURE | `bool portalCheckboxChecked(std::string_view)` |
| `formatRing3Label(char*, size_t, float, bool)` :370 | PURE | unchanged (writes a caller buffer) |
| `formatCurrentRing3Label(char*, size_t)` :380 | PURE (was global-reading) | `formatRing3Label(char*, size_t, const Settings&)` overload |
| `presetFromNm(float)` settings.h:42 | PURE `constexpr` | unchanged |
| `airportToDegrees(const Airport&, double*, double*)` :45 | PURE | `GeoPoint geoPointOf(const Airport&)` |
| `rangeCurrent()` :299 | PURE (was global-reading) | `const RangePreset& Settings::range() const` |
| `rangeIndex()`, `useKm()`, `showRunways()`, `showTerrain()`, `lat()`, `lon()`, `siteCount()`, `siteIndex()`, `siteIdent()`, `siteSlotIdent()`, `siteActiveIdent()` :168-309 | **DELETED** | field reads / `SiteList` const queries |
| `persistSitesString()` :51 | PURE (split) | `void formatSiteIdents(const SiteList&, char*, size_t)`; the `putString` moves to the shell |
| `loadSitesFromStorage()` :86 | PURE (split) | `void parseSiteIdents(std::string_view, SiteList*)` — takes the already-read string |
| `applyActiveSiteCoords()` :71 | PURE | `GeoPoint centerForSites(const SiteList&, GeoPoint fallback)` |
| `rangeNext()` :294 | EFFECTFUL (NVS) | `void cycleRange(AppState&, const SettingsStore&)` |
| `siteNext()` :234 | EFFECTFUL (NVS) | `void cycleSite(AppState&, const SettingsStore&)` |
| `saveSites(const char* const*, size_t)` :243 | EFFECTFUL (NVS + log) | `bool applySites(AppState&, const char* const*, size_t, const SettingsStore&)` |
| `saveLocationFromStrings(...)` :172 | EFFECTFUL | split: pure parse/validate, then `void setCenter(AppState&, GeoPoint, const SettingsStore&)` |
| `saveKmFromPortal`/`saveRunwaysFromPortal`/`saveTerrainFromPortal` :311-327 | EFFECTFUL | `void setToggle(AppState&, Toggle, bool, const SettingsStore&)` |
| `clearLocation()` :193, `unitsReset()` :329 | EFFECTFUL | `resetLocation(AppState&, …)`, `resetOverlays(AppState&, …)` |
| `init()` :144 | EFFECTFUL (NVS) | `Settings loadSettings(const SettingsStore&)` — returns 56 B by value, once |

### 3.3 `src/core/terrain.cpp`

| today | verdict | proposed |
|---|---|---|
| `pointLatLon(...)` :259 | PURE | `GeoPoint gridPointLatLon(GeoPoint, float span, int row, int col)` |
| `terrariumElevation(uint8_t,uint8_t,uint8_t)` :268 | PURE | unchanged |
| `latLonToTilePixel(...)` :281 | PURE | `MercatorPixel mercatorPixel(GeoPoint, int zoom)` |
| `zoomForView(...)` :308 | PURE | `int zoomForView(GeoPoint, float halfSpanKm)` |
| `tilesForView(...)` :335 | PURE | `size_t tilesForView(GeoPoint, float, int, TileId*, size_t)` |
| `buildTileUrl(char*, size_t, const TileId&)` :378 | PURE | unchanged |
| `bandForElevation(int16_t, const int16_t*, int)` :486 | PURE | unchanged |
| `viewCorners(...)` :121 | PURE | `BoundingBox boundsOf(GeoPoint, float)` |
| `progressMatches(...)` :112 | PURE | `bool TileDownload::matches(GeoPoint, uint8_t, float) const` |
| `gridReady(...)` :246 | PURE (was global-reading) | `bool TerrainGrid::covers(GeoPoint, uint8_t) const` |
| `grid(uint8_t)` :252 | PURE (was global-reading) | `state.terrainMatchesView()` + `state.terrain` |
| `mapGridToPixels(...)` :129 | PURE→out-param | `void fillGridAxes(GeoPoint, float, int zoom, int32_t* colPx, int32_t* rowPy)` |
| `beginTile(Grid*, const TileId&)` :146 | PURE→out-param | `void mapTileToGrid(const TileId&, const int32_t*, const int32_t*, TileMapping*)` |
| `onPixel(void*, uint32_t, uint32_t, …)` :186 | PURE, given ctx | same signature; `ctx` becomes a real `TileDecodeContext*` instead of `nullptr` |
| `decodeBody(BodyReader&)` :214 | EFFECTFUL | takes `void* ctx` (Stage 3) |
| `downloadActive()` :237 | PURE (was global-reading) | `bool AppState::downloadInFlight() const` |
| `clear()` :239 | EFFECTFUL | `void invalidateTerrain(AppState&)` |
| `endDownload()` :223, `gateRetries(uint8_t)` :226 | EFFECTFUL (clock + mutate) | `endDownload(AppState&)`, `gateRetries(AppState&, uint8_t, Millis)` |
| `ensureGrid(...)` :382 | EFFECTFUL (HTTP, clock, mutate) | `bool stepTileDownload(AppState&, ViewParams, const ui::ScratchLease&, Millis)` — §4 |
| `setPollFn`, `setPngDecoder` :233-235 | EFFECTFUL (wiring) | plain parameters in `TerrainDeps` |

### 3.4 `adsb.cpp`, `tap_gesture.cpp`, `airport_find.cpp`, `portal_params.cpp`

| today | verdict | proposed |
|---|---|---|
| adsb `kmToNauticalMiles(float)` :23 | PURE | unchanged |
| adsb `readJsonFloat`, `pickNoseHeading`, `pickTrackHeading`, `pickGroundSpeed`, `isOnGround`, `copyJsonStringTrimmed`, `formatAltitudeTag`, `fillTagFields` :25-112 | PURE | unchanged; `fillTagFields(AircraftTarget*, const JsonObject&)` |
| adsb `ElementScanner` :121-195 | PURE (a cursor over a `BodyReader`) | unchanged |
| adsb `parseBody(BodyReader&)` :206 | **PURE given a sink** | `bool parseTraffic(BodyReader&, TrafficStore* out)` — the change that lets the parse be tested without the store |
| adsb `parseResponse(const char*)` :278 | PURE | `bool parseTraffic(std::string_view, TrafficStore*)` |
| adsb `buildUrl(...)` :272 | PURE | `void buildTrafficUrl(char*, size_t, GeoPoint, float radiusKm)` |
| adsb `aircraftCount()`, `aircraftList()`, `clear()`, `setPollFn()` :264-270 | DELETED / EFFECTFUL | field reads; `void clearTraffic(AppState&)` |
| adsb `fetchUpdate(...)` :283 | EFFECTFUL (HTTP + log) | `bool fetchTraffic(AppState&, GeoPoint, float, const Net&)` |
| gesture `tapPress(unsigned long)` :15 | EFFECTFUL (mutates) | `void recordTap(TapRecognizer&, Millis)` — takes the sub-struct, the one mutator small enough to narrow |
| gesture `tapPoll(unsigned long)` :27 | EFFECTFUL (mutates) | `TapKind resolveTap(TapRecognizer&, Millis)` |
| gesture `tapReset()` :43 | EFFECTFUL | `recognizer = TapRecognizer{}` — the mutator disappears |
| airport `normalizeIcao(const char*, char[5])` :14 | PURE | `std::optional<IcaoCode> normalizeIcao(std::string_view)` |
| airport `identCompare` :33, `findAirport(const char*, Airport*)` :37 | PURE | `std::optional<Airport> findAirport(std::string_view)` — `Airport` is 12 B, fine by value |
| portal `isField`, `isSiteField`, `siteFieldSlot` :41-52 | PURE | `Field::is(std::string_view) const`, `Field::isSite() const`, `Field::siteSlot() const` |
| portal `fields()`, `fieldCount()` :56-58 | PURE | `constexpr FieldTable fields()` |
| portal `currentValue(...)` :60 | PURE (was global-reading) | `void formatFieldValue(const Field&, const Settings&, char*, size_t)` |
| portal `htmlAttrs(...)` :81 | PURE (was global-reading) | `void formatFieldAttrs(const Field&, const Settings&, char*, size_t)` |
| portal `applyValue(...)` :101 | EFFECTFUL (NVS via settings) | `void stageField(FormDraft&, const Field&, std::string_view)` |
| portal `applyValueById(...)` :126 | EFFECTFUL | `bool stageFieldById(FormDraft&, std::string_view, std::string_view)` |
| portal `commit()` :139 | EFFECTFUL (NVS + log) | `void commitForm(AppState&, const FormDraft&, const SettingsStore&)` |

`FormDraft` is 78 bytes and lives on the stack of the portal handler on both destinations — it does not belong in `AppState` (it exists for one HTTP POST). That deletes `s_pending_lat`/`s_pending_lon`/`s_pending_sites`, which currently *can* leak between requests.

### 3.5 `src/ui/` — the pure kernels hiding inside the draw calls

| today | verdict | proposed |
|---|---|---|
| radar_display `absDiff` :69 | PURE | inline |
| radar_display `speedLineLengthPx(float)` :225 | PURE | `int speedVectorLengthPx(float gsKnots)` |
| radar_display `noseTip(...)` :244 | PURE | `ScreenPoint noseTip(ScreenPoint, float headingDeg)` |
| radar_display `sortDrawItemsFarFirst` :392 / `sortBeyondDotsFarFirst` :404 | PURE | one `void sortFarFirst(AircraftPlot*, size_t)` — the two are the same insertion sort on structurally identical types |
| radar_display `viewport()` :209 | PURE (was global-reading) | `ViewSpan viewSpan(const Settings&)` |
| radar_display `scaleLabelAnchorX` :566 / `siteLabelAnchorX` :570 | PURE | unchanged |
| radar_display **plot selection** :431-455 | **PURE, and currently not testable** | `struct PlotSet { AircraftPlot plots[64]; ScreenPoint dots[64]; uint8_t plotCount, dotCount; };` + `void planTrafficPlots(const ViewSpan&, const TrafficStore&, PlotSet*)` |
| radar_display compact-tag decision :475 | PURE | `bool useCompactTags(uint8_t plotCount)` |
| radar_display `measureGfxHeight` :71, `measureVlwHeight` :77, `findVlwSizeForHeight` :82, `pickGfxFontClosest` :98 | EFFECTFUL (font engine) | same shape, folded into `LabelMetrics buildLabelMetrics(lgfx::LGFXBase&)` |
| radar_display `initLabelMetrics` :113, `initTagLabelMetrics` :160 | EFFECTFUL | fold into `buildLabelMetrics`, called once |
| radar_display `initPalette` :179 | EFFECTFUL (`tft.color565`) | `Palette buildPalette(lgfx::LGFXBase&)`, called **once** instead of 3× per frame |
| radar_display every `draw*` :220-627 | EFFECTFUL | take `lgfx::LGFXBase&` explicitly; `DrawScope`/`s_draw` deleted |
| radar_display `ensureFrameSprite` :629 | EFFECTFUL (heap) | `bool ensureFrame(AppState&)` — sets `state.frame.available` |
| radar_display `renderFrame` :645, `radarDisplayDraw` :657, `radarDisplayRefreshAircraft` :673 | EFFECTFUL | one `void composeFrame(AppState&, const FrameLease&, …)` |
| radar_display `radarDisplayFrameScratch(size_t)` :684 | EFFECTFUL | replaced by `ScratchLease leaseScratch(AppState&, size_t, PixelSpan)` (§4) |
| runway `e7ToDeg(int32_t)` :71 | PURE | unchanged |
| runway `offsetKmFromCenter` :73, `latLonToScreen` :82, `distSqFromCenter` :96, `clipPointToOuterRing` :102, `clipPointOntoOuterRing` :226, `offsetLabelFromCenter` :212 | PURE, **and duplicates of `core::geo`** | **deleted** — `ViewSpan` methods. runway_overlay.cpp:17 even redefines `kKmPerDeg`. |
| runway `segmentIntersectsDisc(...)` :129 | PURE | `bool ViewSpan::segmentIntersectsDisc(ScreenPoint, ScreenPoint) const` |
| runway `drawRunwayLine`, `drawAirportLabel`, `drawBoldRunwayLabel`, `initRunwayLabelStyle`, `applyRunwayLabelStyle` | EFFECTFUL | take `lgfx::LGFXBase&` + `const ViewSpan&` + `const Palette&` |
| runway airport-in-range scan :267-294 | **PURE** | `void selectRunwaysInRange(const ViewSpan&, float radiusKm, AirportFlags*, uint16_t* labels, uint8_t* labelCount)` |
| terrain_overlay `initPixelToGridMap` :43 | PURE, `constexpr`-able | `constexpr TerrainPixelMap kPixelMap` (§2.4). **Verified `constexpr` under GCC 8.4** |
| terrain_overlay `bandAtPixel(const int32_t*, int)` :64 | PURE | `int bandAtPixel(const int32_t* rowElev, int x, const TerrainPixelMap&)` |
| terrain_overlay row lerp :80-88 | PURE | `void blendGridRow(const TerrainGrid&, int y, const TerrainPixelMap&, int32_t* out)` |
| terrain_overlay `drawScanline` :74 | EFFECTFUL | keeps only the `drawFastHLine` run coalescing |
| terrain_overlay `drawTerrainBackground` :116 | EFFECTFUL | `void drawTerrain(lgfx::LGFXBase&, const TerrainGrid&, const Palette&)` — the gate moves to the caller |
| status_screens all 12 functions :53-246 | EFFECTFUL (display) | unchanged; the 5 statics become one `ConnectingScreen` struct |
| status_screens spinner dot geometry :166-169 | PURE | `ScreenPoint spinnerDot(int i, float headDeg)` |
| display_font all 5 functions | EFFECTFUL (LGFX) | unchanged; `s_vlw_loaded` → `FontCache` |
| **all of `src/platform/**`** | EFFECTFUL by construction | unchanged in kind; Stage 3 adds `void* ctx` to `BodyFn` |
| main.cpp `showRadarIfConnected` :32, `onRangeTap` :41, `onSiteTap` :53, `handleBootButton` :71, `pollWifiAndTaps` :88, `maybeFetchTerrain` :101, `fetchAndDrawAircraft` :125, `setup` :138, `loop` :159 | EFFECTFUL | §6: the **policy** inside them becomes `plan()`, which is PURE |

Head count: **62 functions are pure or trivially made pure**; 71 are effectful, of which 58 are display or platform calls that can only ever be effectful. The most valuable extractions — decision logic with no test today — are `planTrafficPlots`, `selectRunwaysInRange`, `parseTraffic(…, out)` and `plan()`.

---

## 4. Expressing the borrowed-scratch invariant in types

### 4.1 What the invariant actually is

Three claims, all currently enforced by convention:

1. The frame sprite's 115,200 pixel bytes have exactly two consumers: the compositor (radar_display.cpp:645-653) and the PNG decoder (png_decode.cpp:711 via `s_scratch_fn` → radar_display.cpp:684).
2. They may never overlap. Today this holds **only because of what happens to be on the poll path**: `pollWifiAndTaps` (main.cpp:88) calls `wifiLoop()` and latches a tap, and nothing on that path draws. Nothing *prevents* it. Note that on the device `wifiLoop()` → `s_wm.process()` → `onPortalParamsSaved` → `core::portal::commit()` can change lat/lon mid-download; benign today, but that path is one call away from a repaint.
3. After a decode, the pixels are garbage, so a full repaint is **owed**. main.cpp:119-122 discharges that debt by watching the falling edge of `downloadActive()` — a manual obligation with a single, easily-deleted call site.

### 4.2 Proposal — move-only leases minted from the phase

```cpp
// include/ui/frame_lease.h
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/state.h"

namespace ui {

/** A borrowed, non-owning view of the frame sprite's pixel bytes. */
struct PixelSpan {
  uint8_t* bytes = nullptr;
  size_t len = 0;
  bool isEmpty() const { return bytes == nullptr || len == 0; }
};

/**
 * Move-only proof that the frame sprite's pixels are the compositor's for the
 * duration of this object's life.
 *
 * There is no public constructor other than the default one, which yields an
 * INVALID lease that unlocks nothing. The only way to get a valid one is
 * leaseFrame(), which is a friend and which refuses unless
 * AppState::frame.phase == kIdle. Copying is deleted, so a lease cannot be
 * duplicated into a second consumer; moving is allowed so it can be returned
 * from the factory and passed onward. Destruction returns the phase to kIdle.
 *
 * No heap, no exceptions, no virtuals: sizeof(FrameLease) == 12 (verified).
 */
class FrameLease {
 public:
  FrameLease() = default;
  FrameLease(const FrameLease&) = delete;
  FrameLease& operator=(const FrameLease&) = delete;

  FrameLease(FrameLease&& other) noexcept
      : state_(other.state_), pixels_(other.pixels_) {
    other.state_ = nullptr;
    other.pixels_ = PixelSpan{};
  }
  FrameLease& operator=(FrameLease&& other) noexcept {
    if (this != &other) {
      release();
      state_ = other.state_;
      pixels_ = other.pixels_;
      other.state_ = nullptr;
      other.pixels_ = PixelSpan{};
    }
    return *this;
  }
  ~FrameLease() { release(); }

  explicit operator bool() const { return state_ != nullptr; }
  PixelSpan pixels() const { return pixels_; }

 private:
  friend FrameLease leaseFrame(app::AppState&, PixelSpan);
  FrameLease(app::AppState* s, PixelSpan p) : state_(s), pixels_(p) {}
  void release();

  app::AppState* state_ = nullptr;
  PixelSpan pixels_;
};

/** Same contract, opposite borrower: the streaming PNG decoder. */
class ScratchLease {
 public:
  ScratchLease() = default;
  ScratchLease(const ScratchLease&) = delete;
  ScratchLease& operator=(const ScratchLease&) = delete;
  ScratchLease(ScratchLease&&) noexcept;
  ScratchLease& operator=(ScratchLease&&) noexcept;
  ~ScratchLease() { release(); }

  explicit operator bool() const { return state_ != nullptr; }

  /** The decoder's whole memory. nullptr if invalid or too small. */
  uint8_t* bytes(size_t need) const {
    return (state_ != nullptr && pixels_.len >= need) ? pixels_.bytes : nullptr;
  }
  /** Tail past `used`, for download-scoped workspace. See §4.4. */
  uint8_t* tail(size_t used, size_t need) const {
    return (state_ != nullptr && pixels_.len >= used + need)
               ? pixels_.bytes + used
               : nullptr;
  }

 private:
  friend ScratchLease leaseScratch(app::AppState&, size_t, PixelSpan);
  ScratchLease(app::AppState* s, PixelSpan p) : state_(s), pixels_(p) {}
  void release();

  app::AppState* state_ = nullptr;
  PixelSpan pixels_;
};

[[nodiscard]] FrameLease leaseFrame(app::AppState& state, PixelSpan pixels);
[[nodiscard]] ScratchLease leaseScratch(app::AppState& state, size_t need,
                                        PixelSpan pixels);

}  // namespace ui
```

```cpp
// src/ui/frame_lease.cpp
namespace ui {

FrameLease leaseFrame(app::AppState& state, PixelSpan pixels) {
  if (state.frame.phase != app::FramePhase::kIdle || pixels.isEmpty()) {
    return FrameLease{};
  }
  state.frame.phase = app::FramePhase::kComposing;
  return FrameLease{&state, pixels};
}

ScratchLease leaseScratch(app::AppState& state, size_t need, PixelSpan pixels) {
  if (state.frame.phase != app::FramePhase::kIdle || pixels.len < need) {
    return ScratchLease{};
  }
  state.frame.phase = app::FramePhase::kDecodingTile;
  return ScratchLease{&state, pixels};
}

void FrameLease::release() {
  if (state_ == nullptr) {
    return;
  }
  state_->frame.phase = app::FramePhase::kIdle;
  // The frame we just composed is the one on the panel: the debt is settled.
  state_->frame.dirty = false;
  state_ = nullptr;
}

void ScratchLease::release() {
  if (state_ == nullptr) {
    return;
  }
  state_->frame.phase = app::FramePhase::kIdle;
  // The decoder ate the frame. A repaint is now owed, unconditionally, on
  // every exit path — success, HTTP failure, or a truncated PNG. This
  // destructor replaces main.cpp:119-122's falling-edge bookkeeping.
  state_->frame.dirty = true;
  state_ = nullptr;
}

}  // namespace ui
```

### 4.3 How the two consumers are forced through it

```cpp
// The compositor cannot be called without proof. `lease` is unused in the body
// of most helpers — it is a capability, not data.
void composeFrame(app::AppState& state, const ui::FrameLease& lease,
                  lgfx::LGFXBase& sprite, const RenderCache& cache);

// The download step cannot be called without proof either, and it takes the
// scratch lease by const& so it cannot squirrel it away past its own scope.
bool stepTileDownload(app::AppState& state, const ui::ScratchLease& lease,
                      const TerrainDeps& deps, app::Millis now);
```

`platform_png::setScratch` / `s_scratch_fn` (png_decode.cpp:56, 704, main.cpp:152) are **deleted**. `decode()` takes the memory instead of reaching for it:

```cpp
// include/platform/png_decode.h  (proposed)
bool decode(core::platform::BodyReader& body, core::terrain::PixelFn on_pixel,
            void* ctx, uint8_t* scratch, size_t scratch_len);
```

and `core::terrain::PngDecodeFn` grows the same two parameters. The decoder then has no global state at all — which also removes the one reason `test_png` needs to install a scratch provider.

### 4.4 What this buys, precisely

- **Mutual exclusion is unforgeable.** The only constructors that produce a valid lease are private, and their two friends both check the phase. There is no `setPhase()`.
- **The repaint debt cannot be dropped.** It is a destructor, not a call site. Strictly stronger than today: main.cpp:120's `if (ready || (…))` has to be *right*, and a future early `return` before it leaves the panel showing decoder droppings until the next fetch.
- **The dangerous path fails safe.** If anything on the poll path ever tries to repaint mid-download, `leaseFrame` returns invalid, the compose is skipped, and `frame.dirty` stays `true` so the next tick paints. Today that path would silently composite into the decoder's window.
- **One call site.** `radarDisplayDraw()` is called from four places in main.cpp (:37, :49, :65, :120) plus `radarDisplayRefreshAircraft()` (:132). In the target there is exactly one, in `present()` (§6), guarded by one lease.
- **The per-tile tables move into the lease.** `s_col_local`, `s_row_local`, `s_row_first`, `s_col_first` (1,188 B of `.bss`) are alive for exactly one tile — precisely the `ScratchLease`'s lifetime. `sizeof(platform_png::Work)` is 35,840 and the span is 115,200, so `lease.tail(35840, 1188)` is free real estate. Not a trick: it is the lease's lifetime expressing the data's lifetime, which is the point.

### 4.5 What it does **not** buy — be honest

This is a **runtime-checked invariant with a compile-time-guided API**, not a compile-time proof. C++17 has no linear types and no borrow checker, so:

- Nothing stops someone calling `leaseFrame(...)` and ignoring the result — `[[nodiscard]]` makes that a warning, not an error. Add `-Werror=unused-result` to `build_src_flags` for our sources only.
- Nothing stops a *third* party writing to `sprite.getBuffer()` directly. Mitigation: `getBuffer()` is only reachable from `src/ui/frame_lease.cpp` if the sprite object is file-local there — which it should be, and which a CI grep can enforce.
- A lease held across a `return` inside `composeFrame` would be released early and the phase would go idle mid-compose. Passing it by `const&` (never by value, never storing) is the discipline; the deleted copy constructor makes "never stored" mechanical.
- The phase is a 1-byte field, so a wild write corrupts it. True of `g_terrain_download_active` today too; no design fixes that on this device.

### 4.6 Alternative considered and rejected

A `template <FramePhase P>` type-state encoding where `compose()` only accepts `AppState<kIdle>&&` and returns `AppState<kComposing>`. Genuinely compile-time-checked, and unusable here: `AppState` is 6,960 bytes, and moving it between phase types is either a copy (fatal) or a `reinterpret_cast` dressed up as a move (a lie). Rejected on the same grounds as value-returning reducers.

---

## 5. Module / seam map

### 5.1 Proposed layout

```
include/
  config.h                      unchanged
  app/
    state.h                     AppState + FramePhase + FrameState + Session
    plan.h                      Effect / EffectKind / EffectBatch / Environment / plan()
    shell.h                     Clock / Radio / Net / Deps + the effectful steps
  core/
    geo.h                       GeoPoint, ScreenPoint, GroundOffset, ViewSpan   (was geo.h)
    settings_model.h            Settings, SiteList, RangePreset                 (split from settings.h)
    settings_codec.h            parse/format/validate + the NVS key table       (split from settings.h)
    settings_store.h            SettingsStore over KeyValueStore                (new; shell-side)
    traffic_model.h             AircraftTarget, TrafficStore, AircraftPlot      (was aircraft.h)
    traffic_decode.h            parseTraffic(), buildTrafficUrl()               (split from adsb.h)
    traffic_client.h            fetchTraffic()                                  (split from adsb.h)
    terrain_model.h             TerrainGrid, TileId, TileDownload, RetryGate    (split from terrain.h)
    terrain_tiles.h             zoomForView, tilesForView, mercatorPixel, …     (split from terrain.h)
    terrain_fetch.h             stepTileDownload()                              (split from terrain.h)
    gesture.h                   TapKind, TapRecognizer, recordTap, resolveTap   (was tap_gesture.h)
    airports.h                  Airport, Runway, findAirport      (large_airports.h + airport_find.h)
    platform.h                  unchanged in shape; BodyFn gains void* ctx
    portal_params.h             Field, FieldTable, FormDraft
  ui/
    display.h                   unchanged (the one sanctioned #ifdef)
    display_font.h              unchanged
    frame_lease.h               PixelSpan, FrameLease, ScratchLease             (new)
    frame.h                     composeFrame(), ensureFrame(), present()        (was radar_display.h)
    render_cache.h              Palette, LabelMetrics, RenderCache              (new)
    radar_theme.h               constexpr layout + band constants ONLY
    traffic_layer.h             planTrafficPlots(), drawTraffic()      (was inside radar_display.cpp)
    runway_layer.h              selectRunwaysInRange(), drawRunways()  (was runway_overlay.h)
    terrain_layer.h             kPixelMap, blendGridRow(), drawTerrain()  (was terrain_overlay.h)
    status_screens.h            unchanged
  platform/
    png_decode.h                decode() takes scratch as a parameter
    wifi_setup.h                unchanged
    device/…, native/…          unchanged

src/
  main.cpp                      owns THE AppState; setup() / loop() only
  app/plan.cpp                  PURE. Must not include core/platform.h or LovyanGFX.
  app/shell.cpp                 performEffect(), sampleEnvironment(), present()
  core/geo.cpp
  core/settings_codec.cpp       PURE
  core/settings_store.cpp       effectful (KeyValueStore)
  core/traffic_decode.cpp       PURE
  core/traffic_client.cpp       effectful (HttpClient)
  core/terrain_tiles.cpp        PURE
  core/terrain_fetch.cpp        effectful (HttpClient, clock)
  core/gesture.cpp
  core/airports.cpp             (was airport_find.cpp)
  core/large_airports_data.cpp  UNTOUCHED (generated)
  core/portal_params.cpp        PURE after FormDraft
  ui/frame_lease.cpp            owns the LGFX_Sprite; the ONLY file allowed to
                                call sprite.getBuffer()
  ui/frame.cpp                  (was radar_display.cpp) compositor
  ui/render_cache.cpp           buildPalette(), buildLabelMetrics()
  ui/traffic_layer.cpp
  ui/runway_layer.cpp           (was runway_overlay.cpp)
  ui/terrain_layer.cpp          (was terrain_overlay.cpp)
  ui/status_screens.cpp
  ui/display_font.cpp
  platform/…                    unchanged file set
```

### 5.2 Rationale per move

| move | why |
|---|---|
| `core/{settings,terrain,adsb}.h` split into `_model` / pure algorithm / effectful client | Each header today mixes a POD, pure helpers "exposed for unit testing" (settings.h:122, terrain.h:105) and an I/O API. The split makes the pure/effectful line a **file** boundary, so `build_src_filter` and a CI include-grep can police it instead of a comment. `native_test` then links only the model/pure files for most suites. |
| `app/` is new | There is nowhere today for "the loop's policy". It ends up in `main.cpp`'s anonymous namespace, unreachable by tests. `app/plan.cpp` is the single highest-value new test target. |
| `ui/frame_lease.cpp` owns the sprite | The sprite is the shared resource in the invariant. Confining `LGFX_Sprite` and every `getBuffer()` call to one ~60-line file makes §4.5's grep guard trivially true. |
| `ui/render_cache.h` takes the mutable palette out of `radar_theme.h` | radar_theme.h:137-147 declares eleven `extern uint16_t kColor*`: **runtime-mutable variables named as constants**, with external linkage, rewritten by `initPalette()` three times per frame (radar_display.cpp:620, 658, 674). A `Palette` value built once and passed as `const Palette&` fixes the naming lie, the redundant work, and the linkage. |
| `runway_overlay.cpp`'s geometry deleted | runway_overlay.cpp:17 redefines `kKmPerDeg` and :73-127, :226-240 re-implement `offsetKmFromCenter`, `latLonToScreen`, `distSqFromCenter`, `clipPointToOuterRing` that already exist in `core::geo`. Two copies of a projection is exactly the drift the `ViewSpan` methods exist to stop. |
| `large_airports.h` + `airport_find.h` → `airports.h` | `airport_find.h` is 10 lines that exist only to add one function to the generated header's namespace. |
| `ui/radar_range.h` **deleted** | It is a 60-line forwarder to `core::settings` (:31-57). Once state is a field read there is nothing to forward. Its two real functions, `fetchRadiusKm()` (:63) and `terrainHalfSpanKm()` (:75), become pure functions of `RangePreset` in `ui/radar_theme.h`, where their geometry constants already live. |

### 5.3 What stays put, and why

- **`include/config.h`** — already correct: portable, constants only, no state.
- **`include/ui/display.h`** and the `LGFX tft` global. display.h:6-9 documents that this is the single sanctioned `#ifdef`. `tft` is a hardware resource, not application state; putting it in `AppState` would drag LovyanGFX into `app/state.h` and destroy host-testability.
- **`src/platform/` file set** — the shape is already right: one file per concern, per destination, selected by `build_src_filter`. Only `BodyFn`'s signature changes.
- **`src/core/large_airports_data.cpp`** and `scripts/build_large_airports.py` — out of scope, and the arrays are `const` in flash.
- **`wifi_setup_device.cpp`'s WiFiManager plumbing** — 501 lines of Arduino/WiFiManager sequencing, inherently the effect boundary. Only changes: `resetWifiCredentials()` (:197) takes `AppState&`, and the six external-linkage `s_boot_*` symbols get wrapped in the anonymous namespace.
- **`png_decode.cpp`'s internals** — 732 lines of inflate/unfilter that already allocate nothing and hold one 4-byte static. Only `decode()`'s signature changes.
- **`test/` layout and the four `native_test*` envs** — the split exists for real link-order reasons documented in `platformio.ini`. Add suites, not envs.

---

## 6. Effects without `std::function`

### 6.1 The shape

Two ideas, and the first carries the weight:

1. **Dependencies are POD structs of plain function pointers**, passed by value. No `std::function`, no vtables, no DI container, no boot-time registration.
2. **The loop's decision is a pure function returning a tiny value.** Not a type-erased effect; a 2-byte tagged POD. This is what makes the firmware read like a reducer while costing nine bytes.

```cpp
// include/app/plan.h
#pragma once

#include <cstdint>

#include "app/state.h"
#include "core/gesture.h"

namespace app {

enum class EffectKind : uint8_t {
  kNone,
  kMarkDisconnected,     ///< radar went away; drop the visible flag
  kBeginReconnect,       ///< try the saved network
  kShowRadar,            ///< link is up and nothing is on screen yet
  kFetchTraffic,         ///< the ADS-B poll interval elapsed
  kStepTerrainDownload,  ///< a tile is due
  kCycleRange,           ///< single tap
  kCycleSite,            ///< double tap
  kPresentFrame,         ///< frame.dirty and the sprite is free
};

/** 2 bytes. `arg` carries a range/site index where one is needed. */
struct Effect {
  EffectKind kind = EffectKind::kNone;
  uint8_t arg = 0;
};

constexpr size_t kMaxEffectsPerTick = 4;

/** 9 bytes, returned by value. Not a state copy; five words on the stack. */
struct EffectBatch {
  Effect items[kMaxEffectsPerTick] = {};
  uint8_t count = 0;

  bool push(Effect e) {
    if (count >= kMaxEffectsPerTick) {
      return false;
    }
    items[count++] = e;
    return true;
  }
  const Effect* begin() const { return items; }
  const Effect* end() const { return items + count; }
};

/** Everything plan() needs from outside, as plain data. No clock, no radio. */
struct Environment {
  Millis nowMs = 0;
  bool wifiConnected = false;
  core::gesture::TapKind tap = core::gesture::TapKind::kNone;
};

/**
 * THE pure function. Every timing gate that lives in main.cpp:159-196 today —
 * the WiFi-down grace, the reconnect interval, the ADS-B poll interval, the
 * terrain retry gate, the tap dispatch — decided here, with no I/O and no
 * clock. Host-testable by construction: feed it a state and an Environment.
 */
EffectBatch plan(const AppState& state, Environment env);

}  // namespace app
```

```cpp
// src/app/plan.cpp — PURE. Must not include core/platform.h.
#include "app/plan.h"

#include "config.h"

namespace app {

EffectBatch plan(const AppState& state, Environment env) {
  EffectBatch out;

  switch (env.tap) {
    case core::gesture::TapKind::kSingle:
      out.push({EffectKind::kCycleRange, 0});
      break;
    case core::gesture::TapKind::kDouble:
      if (state.settings.sites.isCyclable()) {   // was main.cpp:54
        out.push({EffectKind::kCycleSite, 0});
      }
      break;
    case core::gesture::TapKind::kNone:
      break;
  }

  if (!env.wifiConnected) {
    if (state.session.radarVisible) {
      out.push({EffectKind::kMarkDisconnected, 0});
    }
    // main.cpp:169-171: 0 means "not down yet", so the grace starts now.
    const Millis since = state.session.wifiDownSince == 0
                             ? env.nowMs
                             : state.session.wifiDownSince;
    if (env.nowMs - since >= config::kWifiDownGraceMs &&
        env.nowMs - state.session.lastReconnectMs >=
            config::kWifiReconnectIntervalMs) {
      out.push({EffectKind::kBeginReconnect, 0});
    }
    return out;
  }

  if (!state.session.radarVisible) {
    out.push({EffectKind::kShowRadar, 0});
  } else if (env.nowMs - state.session.lastTrafficFetchMs >=
             config::kAdsbFetchIntervalMs) {
    out.push({EffectKind::kFetchTraffic, 0});
  }

  // The terrain gate, unchanged from main.cpp:101-123 minus the falling-edge
  // repaint: the ScratchLease destructor owns that now.
  if (state.session.radarVisible && state.settings.showTerrain &&
      !state.terrainMatchesView() &&
      !state.retry.blocks(state.settings.rangeIndex, env.nowMs,
                          config::kTerrainRetryIntervalMs)) {
    out.push({EffectKind::kStepTerrainDownload, state.settings.rangeIndex});
  }

  if (state.frame.dirty && state.frame.canLend()) {
    out.push({EffectKind::kPresentFrame, 0});
  }
  return out;
}

}  // namespace app
```

### 6.2 Dependencies as POD function-pointer structs

```cpp
// include/app/shell.h
namespace app {

/** The clock, as data. Lets host tests hand-crank time without a fake link. */
struct Clock {
  Millis (*nowMs)() = nullptr;
  void (*sleepMs)(unsigned long) = nullptr;
};

/** The radio, as data. Mirrors platform/wifi_setup.h, one pointer per function. */
struct Radio {
  bool (*isConnected)() = nullptr;
  bool (*reconnect)() = nullptr;
  void (*pump)() = nullptr;               // wifiLoop
  bool (*consumeTap)() = nullptr;
  void (*pollLongPress)() = nullptr;
};

/** HTTP + the PNG decode seam. Both are already plain function pointers today. */
struct Net {
  bool (*get)(const char* url, core::platform::BodyFn on_body, void* ctx,
              unsigned long timeout_ms, core::platform::PollFn poll) = nullptr;
  core::platform::PollFn poll = nullptr;
  core::terrain::PngDecodeFn decodePng = nullptr;
};

struct Deps {
  Clock clock;
  Radio radio;
  Net net;
  const SettingsStore* store = nullptr;
  void (*log)(const char*) = nullptr;
};

Environment sampleEnvironment(AppState& state, const Deps& deps);
void performEffect(AppState& state, Effect effect, const Deps& deps);
void present(AppState& state, const Deps& deps);
void loopTick(AppState& state, const Deps& deps);

}  // namespace app
```

`sizeof(Deps)` is ~13 pointers = 52 bytes, constructed once in `setup()` and passed by `const&`. Every member is a plain function pointer, so each call is one indirect jump — the same code shape as today's `s_poll_fn`/`s_png_decode`/`PixelFn` seams, which already work on this device. No allocation, no vtable, no `std::function`.

### 6.3 The loop skeleton

```cpp
// src/app/shell.cpp
namespace app {

Environment sampleEnvironment(AppState& state, const Deps& deps) {
  const Millis now = deps.clock.nowMs();
  // Order preserved from main.cpp:160-161 / wifi_setup_device.cpp:435:
  // the button wins over a slow portal request.
  deps.radio.pollLongPress();
  if (deps.radio.consumeTap()) {
    core::gesture::recordTap(state.tap, now);
  }
  deps.radio.pump();                     // wifiLoop(): keeps the portal alive
  return Environment{now, deps.radio.isConnected(),
                     core::gesture::resolveTap(state.tap, now)};
}

void present(AppState& state, const Deps& deps) {
  if (!ensureFrame(state)) {             // creates the sprite once
    composeToPanel(state, deps);         // documented fallback, radar_display.cpp:666
    return;
  }
  const ui::FrameLease lease = ui::leaseFrame(state, framePixels());
  if (!lease) {
    return;  // a decode is in flight; frame.dirty stays true, next tick paints
  }
  composeFrame(state, lease, frameSprite(), renderCache());
  blitFrame();
}                                        // ~FrameLease: phase=kIdle, dirty=false

void performEffect(AppState& state, Effect effect, const Deps& deps) {
  switch (effect.kind) {
    case EffectKind::kNone:
      break;
    case EffectKind::kMarkDisconnected:
      deps.log("WiFi lost — will reconnect\n");
      state.session.radarVisible = false;
      break;
    case EffectKind::kBeginReconnect:
      state.session.lastReconnectMs = deps.clock.nowMs();
      if (deps.radio.reconnect()) {
        state.session.wifiDownSince = 0;
        state.session.radarVisible = true;
        state.frame.dirty = true;
      }
      break;
    case EffectKind::kShowRadar:
      state.session.wifiDownSince = 0;
      state.session.radarVisible = true;
      state.frame.dirty = true;
      break;
    case EffectKind::kFetchTraffic:
      state.session.lastTrafficFetchMs = deps.clock.nowMs();
      if (core::traffic::fetchTraffic(state, state.center(),
                                      fetchRadiusKm(state.settings.range()),
                                      deps.net)) {
        state.frame.dirty = true;
      }
      break;
    case EffectKind::kStepTerrainDownload: {
      // The invariant, in three lines: no lease, no download. And when the
      // lease dies, frame.dirty is true whatever happened inside.
      const ui::ScratchLease lease =
          ui::leaseScratch(state, platform_png::kScratchBytes, framePixels());
      if (!lease) {
        break;
      }
      core::terrain::stepTileDownload(state, lease, deps, deps.clock.nowMs());
      break;
    }
    case EffectKind::kCycleRange:
      core::settings::cycleRange(state, *deps.store);
      state.frame.dirty = true;
      break;
    case EffectKind::kCycleSite:
      core::settings::cycleSite(state, *deps.store);
      core::traffic::clearTraffic(state);
      core::terrain::invalidateTerrain(state);
      // Preserve main.cpp:67's nudge: allow a refetch after kAdsbMinRefetchMs.
      state.session.lastTrafficFetchMs = deps.clock.nowMs() -
                                         config::kAdsbFetchIntervalMs +
                                         config::kAdsbMinRefetchMs;
      state.frame.dirty = true;
      break;
    case EffectKind::kPresentFrame:
      present(state, deps);
      break;
  }
}

void loopTick(AppState& state, const Deps& deps) {
  const Environment env = sampleEnvironment(state, deps);
  const EffectBatch batch = plan(state, env);          // PURE
  for (const Effect& e : batch) {
    performEffect(state, e, deps);
  }
  deps.clock.sleepMs(10);
}

}  // namespace app
```

```cpp
// src/main.cpp — the whole file
#include "app/shell.h"
#include "app/state.h"
// … platform seams …

namespace {
app::AppState appState;                  // THE store. 6,960 B of .bss.
app::Deps deps;
}  // namespace

void setup() {
  core::platform::logInit();
  core::platform::logf("\nPlane Radar\n");
  deps = app::makeDeps();
  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  appState.settings = core::settings::loadSettings(*deps.store);
  if (wifiSetupConnect()) {
    appState.session.radarVisible = true;
    appState.frame.dirty = true;
  }
}

void loop() { app::loopTick(appState, deps); }
```

That is the payoff of the style: `loop()` is one line, `plan()` is a pure function you can write 20 host tests against, and every timing constant that used to be buried in an `if` in `main.cpp` is in one readable switch. `setPollFn`/`setPngDecoder`/`setScratch` (main.cpp:149-152) all disappear.

### 6.4 Why not a real effect *queue*

`EffectBatch` is a batch, not a queue: produced and consumed within one tick, never stored. A persistent queue would need a fill policy and would let effects observe a state their planner never saw. One tick, one plan, immediate execution — at 10 ms per tick nothing needs more. `kMaxEffectsPerTick = 4` is `static_assert`-checkable against the maximum any branch of `plan()` can push (currently 4: tap + show/fetch + terrain + present).

---

## 7. Phased migration path

Every stage compiles for `supermini` **and** `native`, keeps `make test` green, and is flashable. The verification column names the specific check, not "run the tests".

One lever makes the UI stages safe and deserves naming up front: `docs/fidelity-baseline.txt` + `-DPLANE_RADAR_FRAME_HASH` (main_native.cpp:18-92) dumps the derived font metrics and a hash of the composited frame. **Capture it before Stage 0 and diff it after every UI stage.** Without it, "the rendering is unchanged" is an assertion; with it, a test.

### Stage 0 — Guardrails and free wins (no architecture change)

- Record the baseline: RAM 58,668 / flash 1,264,250 / 112 tests / frame hash.
- Delete `s_scale_label_h` and `s_scale_label_max_w` (radar_display.cpp:53-54, 143-152) — dead, §1.10.
- Wrap wifi_setup_device.cpp:23-28 and `onBootButtonIsr`/`initBootButton` in the file's existing anonymous namespace (§1.10 defect 2).
- Add `-Werror=unused-result` to `build_src_flags` for our sources.
- Add `scripts/check-layering.sh`: `src/core/**` must not include `LovyanGFX`/`Arduino`; nothing outside `src/ui/frame_lease.cpp` may call `getBuffer()`; `include/app/state.h` may not contain a non-zero default initialiser and may not include anything outside `core/` and `<cstdint>`. Wire into `make check`.
- Rewrite `.cursor/rules/plane-radar-project.mdc` and the NVS table in `esp32-conventions.mdc`: both describe `services::adsb`, `services::location`, `data::large_airports` as the data layer, and a file `radar_location.cpp` — none of which exist. `esp32-conventions.mdc` still lists `useMiles`, a key settings.cpp:19-24 explicitly warns against reusing.

*Could break:* nothing. *Verify:* build, tests, frame hash identical.

### Stage 1 — Value types and const query methods in `core/` (no state moves)

Rename `Viewport`→`ViewSpan`, `Point`→`ScreenPoint`, `Offset`→`GroundOffset`, add `GeoPoint`; add the eight const methods of §2.2 whose bodies are geo.cpp's existing function bodies verbatim. Rename `Grid`→`TerrainGrid` and add `elevation()`/`covers()`. Rename `Tap`→`TapKind` with `: uint8_t`. Keep the old free functions as one-line `inline` shims for this stage only so `test_geo`/`test_terrain` compile untouched; flip them to the method form as a separate commit at the end of the stage.

*Could break:* nothing material — no layout change (no virtuals added). The `int`→`int16_t` narrowing of `ViewSpan`'s pixel fields is the only behavioural risk; `outer_radius_px` is 107 (radar_theme.h:12) and `center_x/y` 120, so it is safe, and `static_assert(sizeof(ViewSpan) == 32)` pins it. *Verify:* `make test`, frame hash identical, flash/RAM delta ≈ 0 (all bodies inline).

### Stage 2 — Explicit inputs into the UI, while state is still global

Give every draw function its inputs as parameters, built at the top of the compositor from the current globals:

- `drawTerrainBackground(gfx)` → `drawTerrain(gfx, const TerrainGrid&, const Palette&)`
- `drawLargeAirportRunways(gfx)` → `drawRunways(gfx, const ViewSpan&, float radiusKm, const Palette&, AirportFlags*)`
- `drawAircraft()` → `planTrafficPlots(const ViewSpan&, const TrafficStore&, PlotSet*)` + `drawTraffic(gfx, const PlotSet&, const TrafficStore&, const LabelMetrics&, const Palette&)`
- delete `s_draw` and `DrawScope`; thread `lgfx::LGFXBase&` through
- `initPalette()` → `Palette buildPalette(gfx)` called once, not three times per frame
- delete runway_overlay.cpp:17, :71-127, :226-240 in favour of `ViewSpan`

This is where the `ui::radar::kColor*` externs become a `Palette` value and `ui/render_cache.h` appears.

*Could break:* **highest-risk stage in the plan.** Two specific hazards. (a) `initPalette()` currently runs *after* `drawTerrainBackground` and the rings inside `drawStaticGrid` (radar_display.cpp:616-620), so the first frame of a cold boot draws terrain and rings with the *pre-`color565`* literals from radar_display.cpp:24-34 — moving palette construction earlier changes the first frame's colours. Reproduce and decide deliberately. (b) `drawSpeedVector` calls `viewport()` again (:288), so six `ViewSpan` rebuilds per frame become one — correct, but confirm identical. *Verify:* frame hash; eyeball the native harness at all four range presets with terrain on and off. New pure tests for `planTrafficPlots` and `selectRunwaysInRange` land here — the first tests those paths have ever had. **Subdivide this stage by layer** (terrain, runway, traffic, palette): four commits, frame-hash-checked each.

### Stage 3 — `void* ctx` on `BodyFn`, and `parseTraffic(…, out)`

Change `core::platform::BodyFn` to `bool (*)(BodyReader&, void* ctx)` and add `void* ctx` to `HttpClient::get`. Four implementations (http_arduino.cpp:139, http_curl.cpp:85, test_terrain_fetch.cpp:226, plus the typedef at platform.h:166) and two call sites (adsb.cpp:287, terrain.cpp:438). Then make `parseBody` write to an explicit `TrafficStore*` and `decodeBody` to an explicit `TileDecodeContext*`.

This deletes `s_tile_grid`/`s_tile_filled` (terrain.cpp:109-110) and makes `parseTraffic` testable without touching the store. Do it **before** the state moves — it is the mechanism the state moves need.

*Could break:* `test_terrain_fetch`'s scripted `HttpClient::get` must be updated in the same commit; a mismatch is a link error, not a silent bug. *Verify:* `make test`; run `make test-live` here specifically.

### Stage 4 — `AppState` exists; settings, gesture and session move in

Create `include/app/state.h` and instantiate `appState` in `main.cpp`. Move settings.cpp's nine statics, tap_gesture.cpp's three, and main.cpp's five into it. Delete `core::settings`' fourteen accessors and `include/ui/radar_range.h` entirely. Split `settings.cpp` into `settings_codec.cpp` (pure) and `settings_store.cpp` (KeyValueStore). **Keep every NVS key byte-identical** — format changes are Stage 9.

**Cannot be made smaller without making it worse.** Splitting it needs a temporary `AppState* activeState` global for the old accessors to read through, which is a strictly worse intermediate than either end. Accept one large, purely mechanical commit; the compiler finds all ~30 call sites because the accessors are deleted, not deprecated.

*Could break:* the boot ordering. `core::settings::init()` (main.cpp:147) runs *after* `wifiShowsSetupScreenOnBoot()`/`statusScreenPortal()`, but the portal form reads settings (portal_params.cpp:73-75) and on the device `attachPortalParams` (wifi_setup_device.cpp:107) is reached from `ensureWifiManager()` inside `wifiSetupConnect()` (main.cpp:154), i.e. *after* `init()`. Preserve that order exactly. Also `resetWifiCredentials()` (wifi_setup_device.cpp:197) now needs `AppState&`, so the WiFi layer gains a dependency on `app/state.h` — acceptable, it is the shell. *Verify:* `make test` (test_settings' 17 cases retargeted in the same commit), then **flash a real device with existing NVS contents** and confirm lat/lon/range/units/sites/site-index all survive.

### Stage 5 — Traffic store into `AppState`

Move `s_aircraft`/`s_aircraft_count` into `state.traffic`. `fetchTraffic` takes `AppState&` + `Net`. Trivial given Stage 3.

*Could break:* `radar_display.cpp`'s `AircraftDrawItem items[64]` + `dots[64]` are 1,024 + 512 bytes of **stack** per frame (:422-423). Moving them into a `PlotSet` out-param does not change that, but makes the number visible — check it against the Arduino loop task's stack while you are here. *Verify:* `make test` (test_adsb's 10 cases), native harness shows traffic.

### Stage 6 — Terrain store into `AppState`; the leases; one compose call site

Move `s_grid`, `s_grid_range_index`, `s_fail_*`, `s_prog`, `s_col_px`, `s_row_py` into `state.terrain`/`state.download`/`state.retry`. Introduce `ui/frame_lease.{h,cpp}`; move the `LGFX_Sprite` there. `decode()` takes `(scratch, len)`; delete `platform_png::setScratch` and `s_scratch_fn`. Delete `g_terrain_download_active`. Collapse the five draw call sites into `present()`.

This cannot be split from the deletion of `setScratch`: the moment the sprite moves into `frame_lease.cpp`, `radarDisplayFrameScratch` has nothing to return. One commit.

*Could break:* the stage that actually moves the invariant. Two hardware checks. (a) The first tile of a cold-boot download must still succeed — the lease requires `frame.available`, so the sprite must exist before the first `kStepTerrainDownload`, which `present()` on the first tick guarantees *only if* `plan()` orders `kShowRadar` before `kStepTerrainDownload` (it does, §6.1). (b) The repaint-after-decode now happens on the *failure* paths too (HTTP error, truncated PNG, short sample count) — today only two of three repaint, via the falling edge. That is a behaviour **improvement**, but it means one extra full redraw per failed tile, and a full redraw was measured at ~297 ms. With `kMaxTileFailures = 3` and a 60 s retry gate it is fine; confirm it. *Verify:* `make test`, `make test-live`, then on hardware: cold boot with terrain on, watch for `terrain: grid ready`, confirm no `radar: frame sprite alloc failed`, confirm `adsb:` fetches still succeed *after* a terrain download (that is the TLS-heap interaction the invariant exists for), and confirm the largest-free-block figure improved by ~5.6 KB.

### Stage 7 — The pure planner

Extract `plan()` from `main.cpp`, add `app/shell.cpp`, reduce `main.cpp` to §6.3.

*Could break:* subtle timing equivalences. Four to preserve verbatim: `g_wifi_down_since == 0` meaning "not down yet" (main.cpp:169-171); the `kAdsbMinRefetchMs` nudge (main.cpp:67); the ordering `handleBootButton(); wifiLoop();` at the top of `loop()` (main.cpp:160-161); and `fetchAndDrawAircraft()`'s extra `handleBootButton()` after the fetch (main.cpp:130, 133), which exists because the fetch blocks for seconds. *Verify:* the new `test_plan` suite is where confidence comes from — one test per gate, ~20 cases, all pure. This stage should grow the count from 112 to ~140. Then a one-hour soak on hardware: confirm the ADS-B cadence in the log is still 10 s and a WiFi drop still recovers.

### Stage 8 — Cache and workspace cleanup (the RAM win)

`constexpr TerrainPixelMap` (−1,920 B `.bss`); runway `AirportFlags` bitsets (−2,036 B); per-tile resampler tables into `lease.tail()` (−1,188 B); `LabelMetrics`/`Palette`/`ConnectingScreen`/`FontCache` structs.

*Could break:* the bitset rewrite of runway_overlay.cpp:267-294 changes the memo's access pattern; the `constexpr` map narrows `s_cell` from `int` to `int16_t` and `s_frac` from `int32_t` to `int16_t` — safe only because `kFracOne` is 256 and values are 0..256, which the `static_assert`s in the §2.4 prototype pin. **The terrain overlay is the frame's hot loop** (terrain_overlay.cpp:22-32 records 209 ms of a 297 ms frame when it was floats): the `int16_t` loads widen to `int32_t` for the lerp, which on RV32 is one `lh` instead of one `lw` — no cost expected, but measure rather than assume. *Verify:* frame hash **byte-identical**; measure a full redraw before and after on hardware; `riscv32-esp-elf-size` shows the `.bss` drop.

### Stage 9 — Persisted format (the only stage with a migration)

Rename NVS keys with a one-shot migration keyed off a new `schema` u8. Isolated deliberately: its failure mode is *losing a user's settings*, and coupling it to a refactor makes a bisect useless. **Must not be merged into any other stage.**

Keep the two namespaces (`radar`, `planeradar`) — settings.h:57-64 and kv_nvs.cpp:1-8 both document a real NVS handle-conflict hazard that nothing in this design changes. Add `planeradar/schema = 1`; on boot, if absent, read the old keys, write the new ones, write `schema`, `remove()` the old. Suggested names matching the `Settings` fields: keep `radar/lat`, `radar/lon` (no gain in renaming); `rangeIdx`→`range`, `useKm`→`km`, `showRwys`→`runways`, `showTerr`→`terrain`, `sites`→`sites`, `siteIdx`→`site`. NVS keys cap at 15 characters; all fit.

*Verify:* three host tests over `kv_json_file` — old-only, new-only, both-present — plus a hardware test that flashes onto a device configured by the *old* firmware and confirms every setting survives. Then reflash the old firmware and confirm it degrades to defaults rather than misreading (it will, since the old keys are gone). Decide whether that one-way door is acceptable and say so in the release note.

### Stage 10 — Docs

Rewrite `.cursor/rules/*.mdc` against the real layout, refresh `docs/fidelity-baseline.txt`, add a short `docs/architecture.md` explaining the lease and the pure/effectful line — the two things a future reader will otherwise undo.

---

## 8. Risks, and things I would not do

### 8.1 Where the chosen style fits this device badly — candidly

**"Reads functionally" and "one mutable store mutated in place" are in tension, and no amount of naming resolves it.** What enforces purity here is *reachability*, not immutability: a function taking `const AppState&` cannot mutate, and one taking no state cannot even read. That is a real, checkable property — but it is enforced by a CI grep (Stage 0), not by the type system. Anyone reading `void performEffect(AppState& state, …)` and expecting Redux's guarantees will be disappointed. Write it down in `docs/architecture.md` rather than letting the next reader discover it.

**The no-mutating-member-functions rule is right here, and costs something.** It keeps `core/` free of the store, which is what lets `native_test` link `traffic_decode.cpp` without `AppState`. The cost: `SiteList` gets `nextIndex() const` returning a `uint8_t` a free function then assigns — two steps where `sites.advance()` would be one. Pay it; the alternative is `core/` depending on `app/`, which inverts the layering.

**`AppState` as one struct is a coupling bet.** Any test needing a slice of state links the whole 6,960-byte type. Fine because it is header-only POD with no Arduino dependency — but the moment someone puts an `LGFX_Sprite`, a `WiFiManager` or a `std::string` in it, `native_test` stops building. That is the single most likely way this design decays; hence the include-grep in Stage 0.

**The `.bss`→`.data` trap (§2.3) is a real, verified footgun**, and its mitigation makes `AppState` read *less* declaratively than the style wants: `rangeIndex = 0` with a comment instead of `rangeIndex = kDefaultRangeIndex`. A genuine ugliness imposed by the device; comment it at the declaration.

### 8.2 Things I would not do

| would not | why |
|---|---|
| Value-returning reducers (`AppState reduce(AppState, Action)`) | 6,960 bytes copied per action, no FPU, 10 ms tick. Correctly rejected already. |
| Type-erased effects (`std::function<void(Deps&)>`) | Heap-allocates on capture. `Effect` is 2 bytes; `EffectBatch` 9. |
| `std::variant<…>` for `Effect` | It *would* compile (`-fexceptions` is on), but `std::get` pulls in `bad_variant_access` machinery, the layout is not inspectable in GDB over the USB-JTAG session `make debug-device-test` sets up, and a `{enum, uint8_t}` POD is 2 bytes. No upside. |
| `std::optional<TerrainGrid>` | 3,392-byte payload with copy semantics one careless `=` away from a 3.4 KB memcpy. The existing `valid` flag is correct. `optional` is fine for `double`, `ScreenPoint`, `Airport`. |
| A generic `Store<State, Action>` template | One store, one state type. The indirection buys nothing and costs readability. |
| Put `tft`, `LGFX_Sprite` or `WiFiManager` in `AppState` | Resources, not state; they would drag LovyanGFX and Arduino into `app/state.h` and destroy host-testability. §8.1. |
| Rename the `data::large_airports` namespace | Generated by `scripts/build_large_airports.py`, out of scope; renaming means editing the generator and regenerating 10 KB of `.rodata` for a cosmetic gain. |
| Rename `core::adsb` → `core::traffic` *as a namespace only* | I *do* propose it, but honestly: it touches `adsb.h`, `aircraft.h`, `adsb.cpp`, `radar_display.cpp`, `test_adsb`, and `platformio.ini`'s comments. Worth it because "adsb" is the *source* and "traffic" the *domain*, and the `adsb.h` split (§5) has to touch all of it anyway. If the churn is unwelcome, keep `core::adsb` and take only the type renames — the design does not depend on it. |
| Merge the `radar` and `planeradar` NVS namespaces | settings.h:57-64 and kv_nvs.cpp:1-8 document a real NVS handle-conflict hazard nothing here changes. Free rein on *keys*, yes; on namespaces, no. |
| Make `plan()` return the whole next state | See row 1. It returns 9 bytes. |
| Introduce a `Renderer` interface / virtual `draw()` | The brief forbids per-pixel indirection, and this is exactly that. `lgfx::LGFXBase&` is already the polymorphic seam, and `drawStaticGrid` is already a template over `Gfx&` (radar_display.cpp:607) — keep that, it monomorphises. |
| Replace the fixed-point terrain lerp with anything | terrain_overlay.cpp:22-32 records the measurement: floats cost 209 ms of a 297 ms frame. Any abstraction reintroducing `float` there, or an indirect call per pixel, is a regression you will see with your eyes. |
| Move `s_pending_*` (portal_params.cpp:37-39) into `AppState` | They live for one HTTP POST. `FormDraft` on the handler's stack is 78 bytes and cannot leak between requests, which the statics currently can. |
| Touch `png_decode.cpp`'s inflate | 732 lines, allocation-free, 26 host tests, one 4-byte static. Only the signature changes. |

### 8.3 Residual risks

1. **The frame-hash baseline is the only automated guard on Stages 2, 6 and 8.** If it drifts for an unrelated reason (a LovyanGFX bump, a font change) the safety net is gone. `platformio.ini`'s pinned `lovyan03/LovyanGFX@1.2.26` protects this; do not relax it during the refactor.
2. **Stage 4's device-NVS test cannot be automated.** It needs a physical device configured by the current firmware. Do it before Stage 5, not after Stage 9.
3. **The extra repaint on failed tiles** (Stage 6) is a behaviour change I am proposing deliberately. Confirm the ~297 ms cost is acceptable at `kMaxTileFailures = 3`.
4. **`Environment` sampling reorders `handleBootButton()` and `wifiLoop()`** relative to main.cpp:160-161. It should not matter — `wifiLoop()` calls `bootButtonPollLongPress()` itself (wifi_setup_device.cpp:435, mirrored deliberately at wifi_setup_native.cpp:199) — but it is exactly the ordering a long BOOT hold during a slow portal request would expose. Keep `pollLongPress(); consumeTap(); pump();` as written in §6.3.
5. **`s_in_wifi_loop`** (wifi_setup_native.cpp:62) guards `wifiLoop()` against re-entering itself via the `PollFn` seam. `Deps.radio.pump` preserves that seam exactly, so the guard must survive. Do not "simplify" it away.
6. **`int16_t` narrowing** appears in three places (`ViewSpan` pixel fields, `TileId`, `TerrainPixelMap::frac`). Each is pinned by a `static_assert` or a documented bound, and each is a silent-wraparound risk if a future change raises `kSize`, `kMaxZoom` or `kFracBits`. Add `static_assert(kFracOne <= INT16_MAX)` and friends next to the *constants*, not in the model header.

---

## Appendix — verification artefacts

All produced with the project's own toolchain, not a host compiler:

```
$ riscv32-esp-elf-g++ --version
riscv32-esp-elf-g++ (crosstool-NG esp-2021r2-patch5) 8.4.0

$ riscv32-esp-elf-g++ -std=gnu++17 -fno-rtti -Os -Wall -Wextra -c proto_state.cpp
COMPILES CLEAN          # §2.2 models, §4.2 leases, §6.1 effects,
                        # constexpr TerrainPixelMap, AirportFlags

$ riscv32-esp-elf-gcc-nm --print-size -C proto_state.o
00000000 000019e8 D theState        # 6,632 = AppState without the axis tables

$ riscv32-esp-elf-size -A bss2.o    # AppState with all-zero defaults
.data 0     .bss 6632

$ riscv32-esp-elf-size -A bss_test.o  # AppState with non-zero defaults
.data 13264 .bss 0                    # <- the trap in §2.3

$ riscv32-esp-elf-gcc-nm --print-size -C bss_test.o | grep kPixelMap
00000000 000003c0 r ui::terrain::kPixelMap   # 960 B in .rodata, i.e. flash
```

`static_assert`s that passed, and are therefore load-bearing claims: `sizeof(GeoPoint)==16`, `sizeof(ViewSpan)==32`, `sizeof(AircraftTarget)==48`, `sizeof(TrafficStore)==3076`, `sizeof(AircraftPlot)==12`, `sizeof(TerrainGrid)==3392`, `sizeof(RetryGate)==8`, `sizeof(SiteList)==32`, `sizeof(Settings)==56`, `sizeof(TapRecognizer)==8`, `sizeof(Session)==16`, `sizeof(FrameState)==3`, `sizeof(AppState)==6632`, `sizeof(Effect)==2`, `sizeof(EffectBatch)==9`, `sizeof(FrameLease)==12`, `sizeof(TerrainPixelMap)==960`, `sizeof(AirportFlags)==296`, `!__is_constructible(FrameLease, const FrameLease&)`, `__is_constructible(FrameLease, FrameLease&&)`, `kPixelMap.cell[0]==0`, `kPixelMap.cell[239]==kGrid-2`, `kPixelMap.frac[239]==kFracOne`.

Not verified, flagged as such:
- `sizeof(TileDownload)==392` — the 64-byte version was verified; the +328 axis tables are arithmetic, not a compiler measurement.
- The Stage 8 claim that `int16_t` grid loads cost nothing on RV32 — reasoned from the ISA, not measured. Measure it.
- The Stage 6 claim that largest-free-block improves by ~5.6 KB — follows from the `.bss` reduction but depends on where the allocator's arena starts. Measure with `heap_caps_get_largest_free_block()`.
