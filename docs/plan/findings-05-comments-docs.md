# Findings 05 — Comment audit and documentation migration plan

Scope: every comment in `src/` and `include/`, excluding generated `src/core/large_airports_data.cpp`. Read-only audit; no source file was touched.

**Headline numbers.** 55 non-generated translation units, 7,670 lines, **1,326 comment lines (17%)**. Of those, **~815 are RATIONALE**. **~230 are API CONTRACT**. **101 are section markers** (39 banners, 63 namespace closers). Only **~170 genuinely restate the code.** Projected after the strip: **~500 comment lines (~7%)**.

That ratio is the most important finding here. This is not a codebase with a redundant-comment problem; it is a codebase whose comments are a design record. A mechanical strip that treats density as the metric destroys ~815 lines of irreplaceable knowledge to remove ~170 lines of actual redundancy.

## 1. Buckets, decision rules, and one deviation

| Bucket | Rule applied | Count |
|---|---|---|
| **RESTATES** | Says in English what the next line says in C++; no constraint, unit, or reason. Delete. | ~170 |
| **RATIONALE** | Explains *why*, records a measurement, names a rejected alternative, or states an invariant the type system does not enforce. **Never deleted without a destination.** | ~815 |
| **API CONTRACT** | Parameters, ownership, units, valid ranges, error behaviour of a declaration in `include/` (or a non-obvious internal helper). | ~230 |
| **TODO/FIXME/HACK** | Actionable outstanding work. | 1 |
| **SECTION MARKER** | `// --- Foo ---` banners (39) + `}  // namespace X` closers (63). | 101 |

### Decision: API CONTRACT stays in the header

**Recommendation: keep API CONTRACT comments in `include/`. Do not migrate them to `docs/modules/`.**

1. They are not redundant. `float half_span_km` does not say "centre to screen edge, km"; `int16_t elev_m[kGridPoints]` does not say "row-major, row 0 = north edge". Deleting them deletes information that exists nowhere else, including in the code.
2. They are read at the point of use, with hover-docs. A pointer to `docs/modules/terrain.md#ensuregrid` costs a context switch on every call-site lookup, and the whole value of a contract is that it is free to consult.
3. They do not rot the way rationale does — a contract comment sits on the declaration it describes, so a signature change puts it in the diff. Rationale in a `.cpp` two hundred lines from what it justifies is what actually goes stale.
4. Duplicating them into docs creates two sources of truth, strictly worse than either alone.

`docs/modules/*.md` should describe *mechanisms and reasons* and link to headers for signatures. Where a header comment mixes contract and rationale (common: `include/core/terrain.h:84-98`, `include/platform/png_decode.h:54-62`), split it — contract stays, the rationale sentence becomes a pointer.

### Deviation from the agreed docs layout — please decide

Roughly **210 RATIONALE lines fit none of the five agreed files**: the native harness (`button_sdl.cpp`, `display_native.cpp`, `font_blob_file.cpp`, `http_curl.cpp`, `kv_json_file.cpp`, `main_native.cpp`, `platform_native.cpp`, `wifi_setup_native.cpp` — 155 lines) and the config portal (`portal_params.{h,cpp}`, `portal_server.{h,cpp}`, `wifi_setup_device.cpp` — 130 lines, some overlap).

- **(A) Recommended: add `docs/modules/native-harness.md` and `docs/modules/portal.md`.** Two more files, and every pointer resolves.
- **(B) Fold both into `ARCHITECTURE.md`.** Keeps the agreed file list, but ARCHITECTURE.md becomes ~600 lines and stops being the "human-facing project structure" doc.

**§5 assumes (A).** If you pick (B), rewrite `docs/modules/native-harness.md#x` → `ARCHITECTURE.md#native-x` and `docs/modules/portal.md#x` → `ARCHITECTURE.md#portal-x`; anchors are pre-namespaced to make that a find-and-replace.

### Section markers: delete banners, keep namespace closers

Delete all 39 `// --- Foo ---` banners. **Keep all 63 `}  // namespace X` closers** — Google-style convention, clang-format won't remove them, and removing them is churn with no benefit. They are counted in the "after" projection.

## 2. Where the two critical constraints currently live

Both are **duplicated across source and README, with numeric drift.**

### Constraint 1 — memory / borrowed scratch

| Location | What it says |
|---|---|
| `include/platform/png_decode.h:3-25` | Full statement: 115 KB sprite + ~30 KB TLS + **"a ~44 KB decoder"**, ~13 KB over heap; freeing fails, **114,676 vs 115,200**, TIME_WAIT; borrowing dissolves it |
| `include/platform/png_decode.h:35-43` | `kScratchBytes` arithmetic: 32768 + 2048 + 1024 |
| `src/platform/png_decode.cpp:40-44,53-54` | `Work` struct + `static_assert(sizeof(Work) <= kScratchBytes)` |
| `include/ui/radar_display.h:14-25` | Lender's side; "contents come back as garbage, caller must repaint" |
| `include/config.h:67-78` | https mandatory → TLS cost → why the decoder borrows |
| `src/core/terrain.cpp:29-37` | ~45 KB heap with sprite live; 13 KB of `.bss` decides whether TLS can open |
| `src/core/terrain.cpp:218-222` | `endDownload()` — "those borrowed pixels were the frame" |
| `src/main.cpp:114-118` | Repaint on the download-stop edge |
| `src/platform/png_decode.cpp:713-715` | No-scratch path |
| `README.md:122-124` | Same story, **"a decoder (~36 KB)"** |
| `platformio.ini:161-163` | "the decoder is ours and allocates nothing" |

> **Numeric inconsistency to resolve.** `png_decode.h:9` says **~44 KB**; `README.md:122` says **~36 KB**; `kScratchBytes` is **35,840** and `sizeof(Work)` computes to **35,520**. Correct figure: **~35 KB**. The §3 draft uses 35 KB *with the derivation* so it cannot drift again. Fix `README.md:122` in the same pass.

### Constraint 2 — no FPU

| Location | What it says |
|---|---|
| `src/ui/terrain_overlay.cpp:22-32` | 57,600 px/frame, **209 ms of a 297 ms frame** in float; 8 fractional bits; why 8 is enough |
| `src/ui/terrain_overlay.cpp:1-8` | "on a core without an FPU makes the arithmetic below the redraw's hot loop" |
| `README.md:128` | 209 ms vs **37 ms** in integers |
| `README.md:94-96` | `sinf`/`cosf`/`atan2f` soft-float; 1 px divergence device vs native |

> **The 37 ms fixed-point figure exists only in README.md.** The source comment gives 209/297 but not the after-number. `constraints.md` must carry both or the "how much did we win" half is lost.

### Other named knowledge — where it actually lives

| Knowledge | Location | Status |
|---|---|---|
| `--no-stub` at 115200, esptool stub dies on C3 USB-Serial-JTAG | `platformio.ini:25-26` (bare flags, **no comment**), `README.md:284` | Source has no comment; README is the only copy |
| **Serial monitor must be closed or upload fails** | **nowhere** | **UNDOCUMENTED. Add to constraints.md.** |
| `large_airports_data.cpp` generated | `src/core/large_airports_data.cpp:1`, `include/core/large_airports.h:1` | Present, keep inline |
| Tests must not link SDL — SDL2 `#define`s `main` | `platformio.ini:175-185` | In platformio.ini only, not in `test/` |
| One terrain grid cached; per-preset removed | `src/core/terrain.cpp:29-37` (says **13 KB**), `include/core/terrain.h:96-97` | Brief said ~10 KB, source says 13 KB — source is right (4 slots × 3,362 B = 13.4 KB; 3 spare = ~10 KB). Use "three spare slots, ~10 KB" and show the arithmetic. |

## 3. `docs/constraints.md` — full draft

Commit this file **first**, before any comment is removed, so every pointer resolves.

````markdown
# Constraints

Hard limits of the ESP32-C3 target and the things that have already been tried
and measured. Everything here was learned the expensive way. **If you are about
to change memory allocation, per-pixel arithmetic, storage keys, or the flashing
flags, read the relevant section first.**

Anchors in this file are referenced from one-line pointers in the source. If you
delete a section, grep for its anchor first.

---

## The device {#the-device}

- ESP32-C3, single RISC-V core, **no FPU**, 4 MB flash, ~400 KB SRAM of which the
  Arduino/ESP-IDF/WiFi stack leaves a fraction usable.
- 240×240 round GC9A01 over SPI, `pin_miso = -1` — **the panel cannot be read
  back.**
- One app partition, 0x300000, no OTA slot (`partitions/plane_radar.csv`).
- Everything runs in one cooperative `loop()`. There is no second thread to hide
  latency in, so a 300 ms frame is a 300 ms freeze.

---

## Memory: the budget {#memory-budget}

Three things want RAM at the same moment, and they do not fit:

| Consumer | Size | Lifetime |
|---|---|---|
| Frame sprite, 240×240 × 16 bpp | **115,200 B** | allocated once at boot, never freed |
| TLS session for one HTTPS request | **~30 KB** | duration of a request |
| PNG/inflate working set | **35,840 B** (`kScratchBytes`) | duration of one tile decode |

With the sprite live the heap holds roughly **45 KB**. A TLS session takes ~30 KB
of that. The decoder's 35 KB does not fit in what is left — the three together
are about **13 KB more than the heap has**.

Consequences, each with its own section: the decoder borrows the sprite instead
of allocating ([#borrowed-scratch](#borrowed-scratch)); freeing the sprite for the
download **does not work** and we have the measurement
([#freeing-the-sprite-fails](#freeing-the-sprite-fails)); response bodies are
never buffered whole ([#no-whole-body-buffering](#no-whole-body-buffering)); and
exactly one terrain grid is cached, not one per range preset
([#one-terrain-grid](#one-terrain-grid)).

---

## Borrowed scratch: the PNG decoder allocates nothing {#borrowed-scratch}

`src/platform/png_decode.cpp` is our own inflate + PNG unfilter rather than a call
into LovyanGFX's bundled pngle, and its defining property is that **it allocates
not one byte.** The caller lends it the memory it needs:

```
main.cpp:  platform_png::setScratch(ui::radarDisplayFrameScratch);
```

`ui::radarDisplayFrameScratch()` hands back **the frame sprite's own pixel
buffer**. While a tile is decoding, those 115,200 bytes are the decoder's
workspace. Nobody is looking at them: the panel is still showing the last frame
that was blitted.

Nothing is allocated at run time, so **nothing can fail to be allocated.**

### The scratch layout

`kScratchBytes = 32768 + 2048 + 1024 = 35840`:

| Bytes | What | Why that size |
|---|---|---|
| 32768 | DEFLATE sliding window | RFC 1951 fixes it; not negotiable |
| 2048 | two unfiltered scanlines | a 256 px RGB row is 768 B; PNG filters need the row above |
| 1024 | Huffman decode tables | literal, distance and code-length trees as `count[]`/`symbol[]` — no pointers, no tree nodes |

The real figure is `sizeof(Work)` = **35,520 B**. `kScratchBytes` is the rounded
budget, and the two are tied together by

```cpp
static_assert(sizeof(Work) <= kScratchBytes,
              "kScratchBytes no longer covers the decoder's working set");
```

so the header's promise to callers is **checked by the compiler, not trusted.** If
you add a field to `Work`, that assert is what tells you. Do not "fix" a failing
assert by raising `kScratchBytes` past 115,200 — that is the sprite's size, and
the lender returns `nullptr` above it.

### THE INVARIANT

> **Nobody may compose a frame while a tile is decoding.**

This is not enforced by any type. It holds because both happen in the same
single-threaded `loop()`, and because `core::terrain::ensureGrid()` returns
between tiles. Two ways to break it, both of which compile:

- Moving the terrain fetch onto a task, a timer, or an ISR.
- Repainting from inside the poll hook (`wifiLoop()`) that runs *during* the HTTP
  transfer.

The decode leaves the sprite full of the decoder's workings, so **the caller must
repaint the whole frame after a download stops** — on success to show the new
terrain, on failure to clean up after it. `src/main.cpp` does this on the edge
where `core::terrain::downloadActive()` goes false, deliberately only on the edge:
the retry gate holds that flag false for a minute at a time and repainting every
loop would be a full redraw every 10 ms.

---

## Freeing the sprite for the download: tried, measured, does not work {#freeing-the-sprite-fails}

The obvious fix is to free the 115 KB sprite before the request and recreate it
afterwards. **This was implemented and it fails, reproducibly.**

What happens: the TLS/TCP path allocates inside the hole the free leaves. Only a
few hundred bytes, but they land in the middle of it, and TCP **`TIME_WAIT`** keeps
the connection state — and therefore those bytes — alive well past the end of the
request. When the sprite is recreated, the largest free block comes back as

```
114,676 available   against   115,200 needed
```

524 bytes short. `createSprite()` fails. **The sprite never comes back.** From that
point on every frame is painted directly onto the panel, which means visible
progressive redraw: you watch the rings, then the runways, then the aircraft
appear, and the labels flicker as they are erased and rewritten. The device does
not crash, it just permanently looks broken — which is why this was not caught
immediately.

Recreating it later does not help either; the fragmentation is not transient.

**Do not re-attempt this.** If you think you have a way around it (allocating the
sprite from a different heap region, forcing `SO_LINGER`, retrying after
`TIME_WAIT` expires), the burden is a measurement of `largest free block` after a
real tile fetch on hardware, not a compile.

---

## No FPU: per-pixel arithmetic must be integer {#no-fpu}

The ESP32-C3 core has **no floating-point unit.** Every float operation is a
soft-float library call — `__addsf3`, `__mulsf3`, and so on. A float multiply is
not "a bit slower than an int multiply"; it is a function call with a prologue.

### The measurement

The terrain upsample bilinearly interpolates the 41×41 elevation grid up to the
full frame. That is **57,600 pixels per redraw**, each needing two lerps.

| Implementation | Cost |
|---|---|
| float weights | **209 ms** of a **297 ms** frame |
| fixed point, 8 fractional bits | **37 ms** |

209 ms was the single largest cost in the whole redraw — larger than every draw
call put together. The same algorithm in fixed point is **5.6× faster** and the
output is indistinguishable.

### The rule

> **Anything that runs per pixel stays integer-only.**

`ui::terrain` (`src/ui/terrain_overlay.cpp`) uses `kFracBits = 8`. Why 8 is right:

- The weight error is below 1/256 of a grid cell — far under one screen pixel, so
  the interpolation is visually exact.
- Elevations are carried in whole metres, and the hypsometric bands are hundreds
  of metres apart. Sub-metre precision would be measuring nothing.
- Whole metres keep both lerps inside `int32_t` for any pair of terrarium values,
  including absurd ones, so neither can overflow.

Float is fine **outside** per-pixel code: aircraft geometry runs at most 64 times a
frame, font-metric binary searches run once. `sinf`/`cosf`/`atan2f` there are
soft-float on device and macOS libm natively, which is why trig-derived aircraft
positions can differ by 1 px between destinations while text metrics and integer
layout are exact (see `docs/fidelity-baseline.txt`).

---

## Never buffer a whole response body {#no-whole-body-buffering}

An adsb.fi reply is tens of kilobytes at the wider range presets. **A single
allocation that size throws `std::bad_alloc`, which aborts the firmware.** This
happened; it is why `core::platform::BodyReader` exists.

`HttpClient::get()` hands the body to a `BodyFn` as a pull-based `BodyReader`
instead of returning a string. Nothing downstream may hold more than a window:

- `core::adsb::parseBody()` walks the `"ac"` array one element at a time; peak RAM
  is one aircraft-sized `JsonDocument`.
- Device transport reads the socket through a 512-byte window
  (`StreamBodyReader`).
- The terrain PNG decoder decodes straight off the body; a 60–150 KB tile never
  lands in the heap at all, and tile *size* costs nothing.

`read()` and `readBytes()` carry exactly the signatures ArduinoJson's deserializer
expects, so a `BodyReader&` can be passed straight to `deserializeJson()`.

Natively (`http_curl.cpp`) curl buffers and replays through `MemoryBodyReader` — on
the host that is free. Do not "simplify" the device path to match it.

---

## One terrain grid is cached, not one per range preset {#one-terrain-grid}

`core::terrain` keeps exactly one `Grid` — the view on screen. A per-preset cache
was implemented and **removed.**

The arithmetic: a slot is `41 × 41 × 2 B` = **3,362 B**. Four presets would be
**13.4 KB of `.bss`**, i.e. ~10 KB more than we keep. That is not spare change
here: with the sprite live the heap holds about 45 KB and a TLS session needs
~30 KB of it, so those 10 KB decide **whether the ADS-B fetch can open a socket at
all** ([#memory-budget](#memory-budget)).

The price of not caching is that changing range re-downloads: 1–4 tiles, about a
second, with the radar still running. That is the cheaper side of the trade.

Do not reintroduce a per-preset cache without re-measuring the largest free block
during a TLS handshake.

---

## Storage keys and NVS namespaces are frozen {#frozen-storage-keys}

### Namespaces stay separate

| Namespace | Keys | Written by |
|---|---|---|
| `wifi` | `portal` | `src/platform/device/wifi_setup_device.cpp` |
| `radar` | `lat`, `lon` | `src/core/settings.cpp` |
| `planeradar` | `rangeIdx`, `useKm`, `showRwys`, `showTerr`, `sites`, `siteIdx` | `src/core/settings.cpp` |

**Do not merge them, and do not hold a `Preferences` handle open across calls.**
Overlapping handles cause NVS handle conflicts. `src/platform/device/kv_nvs.cpp`
opens and closes its own handle around every single operation, via an RAII guard so
no error path can leak one. The namespace is a call parameter for exactly this
reason.

### Key names are frozen

Changing a key strands every already-configured device on its next firmware
update: the setting reads as absent and silently reverts to default.

One key deserves a specific warning. **`useKm` is deliberately not the old
`useMiles`.** `useMiles` meant km-vs-statute-miles; `useKm` means NM-vs-km.
Reusing the old key would have **silently inverted the preference** on every device
in the field. If you rename a setting whose meaning changed, take a new key.

### `unitsReset()` is deliberately asymmetric

A Wi-Fi credential wipe resets units and the runway/terrain overlays but
**deliberately does not reset the range preset.** The user's chosen zoom survives.
Covered by `test/test_settings/test_settings.cpp`.

---

## Flashing and the serial monitor {#flashing-no-stub}

```ini
upload_flags = --no-stub
upload_speed = 115200
```

Both are load-bearing and neither is a preference.

- **`--no-stub`.** Over the ESP32-C3's built-in USB Serial/JTAG, esptool's flasher
  stub stops responding right after `Stub running...` and the upload dies with
  `Unable to verify flash chip connection`. The ROM loader takes ~40 s instead of
  ~15 s, but it does not fail.
- **115200.** Pinned for the same reason: baud renegotiation is another failure
  point, and the nominal rate is meaningless over USB CDC anyway.

> **Close the serial monitor before uploading.** The monitor holds the USB CDC
> port; with it open the upload fails to acquire the device. `make monitor` and
> `make upload` cannot overlap. This is the most common "the board is broken" false
> alarm.

Related, and equally cheap to trip: `Failed to read MISA from hart 0` means the
RISC-V debug module is wedged. It looks exactly like dead hardware. **No reset
clears it — only unplugging the board does**, because every reset available over
USB leaves the chip powered. Two things put it there: flashing with esptool (hence
`make upload` / `make flash-release` need a power-cycle before a debug session),
and ending a debug session with the CPU halted (hence `scripts/device-stop.sh`
resumes the target on `EXIT`, `INT` and `TERM`).

---

## Tests must not link SDL or LovyanGFX {#tests-must-not-link-sdl}

**SDL2's macOS headers `#define main`.** Any test binary that reaches an SDL
header — directly, or transitively through LovyanGFX — gets **Unity's entry point
renamed**, and the suite either fails to link or silently runs nothing.

So `env:native_test_png` builds `platform/png_decode.cpp` and nothing else:

```ini
build_src_filter =
  -<*>
  +<platform/png_decode.cpp>
```

No `core/` (the decoder touches only headers from it), no `platform_native.cpp` (so
the test file can stub `logf()` into silence and twenty-odd decodes stay readable),
and above all no `ui/`. `env:native_test_live` is the same shape.

**Do not add an include to a test that pulls in `ui/`, `LovyanGFX.hpp`, or
`lgfx_config_*.hpp`.** If a test needs to draw, it belongs in a different env.

The decoder being ours and allocating nothing is what makes this possible: on the
device its scratch is the frame sprite's pixels, and in the test it is a static
array.

---

## Generated sources {#generated-sources}

**`src/core/large_airports_data.cpp` and `include/core/large_airports.h` are
generated. Never hand-edit them.**

Source: OurAirports CSV exports, via `scripts/build_large_airports.py` (needs
network). Filters: `type == large_airport`, runways not `closed`, helipad
designators (`H*`) excluded. Coordinates are `int32_t` e7 fixed point,
`round(deg * 1e7)`.

To change the data, change the script and regenerate:

```bash
python3 scripts/build_large_airports.py
```

`test/test_png/png_fixtures.h` is likewise generated, by
`scripts/gen_png_fixtures.py`.

---

## The font blob pointer must outlive the program {#font-blob-lifetime}

`LGFXBase::loadFont()` **retains the pointer it is given** — it does
`_font_data.set(array)` and reads from it lazily, for the lifetime of the program.
`core::platform::fontBlobData()` must therefore return immortal storage.

- Device: backed by linker symbols from PlatformIO's `board_build.embed_files`.
  Storage is in flash, so this is free.
- Native: a function-local `static` buffer, loaded once from `data/ui_font.vlw` and
  **never resized after the first load.**

A local or reallocated buffer leaves every `textWidth()` / `drawString()` call
reading freed memory — and it would usually *appear* to work, which is the worst
possible failure mode for a harness whose entire purpose is faithful font metrics.

---

## `reboot()` is a real process exit natively {#reboot-is-process-exit}

Device: `esp_restart()`. Native: **`exit(0)`, deliberately not an in-process
re-entry of `setup()`.** Two reasons:

1. Guard statics would survive it — `s_frame_ready`, `s_label_metrics_ready`,
   `s_tag_label_metrics_ready` in `radar_display.cpp`, `s_vlw_loaded` in
   `display_font.cpp` — so the "reboot" would stop reproducing a cold boot, which
   is the only thing it exists to reproduce.
2. It is reached from inside a nested
   `loop() → wifiLoop() → bootButtonPollLongPress()` chain, which must not be
   allowed to recurse.

---

## Layering rules {#layering}

- **`core/` must not include Arduino, ESP-IDF or LovyanGFX.** It compiles for both
  destinations and is host-testable. Platform needs go through the
  `core::platform` seam; drawing needs are injected
  (`core::terrain::PngDecodeFn` is wired to `platform_png::decode` in `main.cpp`).
- **`include/config.h` must stay free of Arduino/ESP-IDF headers.** Device-only pin
  and bus settings live in `include/platform/device/pins.h`, which pulls
  `<driver/gpio.h>` and therefore may only be included by the device build.
- **`include/ui/display.h` holds the single sanctioned `#ifdef` in shared code.**
  Every other platform difference is a whole file selected by `build_src_filter`.
  Do not add a second one.
- **`src/platform/native/portal_server.h` is private** to
  `src/platform/native/`. Nothing outside it may depend on that header.

---

## Native harness: the SDL main-thread rule {#native-main-thread}

Homebrew's `sdl2` is **sdl2-compat**, which serves the SDL2 API from SDL3, and SDL3
enforces AppKit's rule that windows may only be touched from the main thread.
`Panel_sdl::main()` owns that thread and runs `setup()`/`loop()` on a worker.

**Any SDL window call made from shared code aborts the harness** with
`NSWindow geometry should only be modified on the main thread!`. Configure the
window *before* `tft.init()` — with no window yet, `Panel_sdl` only records the
setting and `SDL_CreateWindow` applies it on the main thread.

---

## Hardware gotcha: GPIO2 is a strapping pin {#gpio2-strapping}

`kDisplayPinDc = GPIO_NUM_2`. GPIO2 is an ESP32-C3 strapping pin and **must not be
held LOW at reset.** This is safe here only because the panel's DC input is
high-impedance and never drives it. Do not reassign a driving output to GPIO2.
````

## 4. Destination doc outlines and anchors

### `ARCHITECTURE.md` (repo root)

| Anchor | Content |
|---|---|
| `#what-it-is` | ESP32-C3 + GC9A01 ADS-B radar, two destinations from one source tree |
| `#layer-map` | Real layer table (corrected version in §9) |
| `#namespaces` | `config`, `core::{adsb,settings,geo,terrain,gesture,platform,airport,portal}`, `ui::{radar,runway,terrain}`, `platform_png`, `data::large_airports` |
| `#portability-seam` | `core::platform` — clock, log, reboot, KV store, HTTP, font blob, portal hints; one impl per destination under `src/platform/{device,native}/` |
| `#the-single-ifdef` | `include/ui/display.h`; everything else is `build_src_filter` |
| `#injected-seams` | `PngDecodeFn`, `PollFn`, `ScratchFn` — why `core/` injects instead of calling |
| `#boot-flow` | `setup()` order, and why `platform_png::setScratch` is wired before the first draw |
| `#main-loop-contract` | `wifiLoop()` every iteration; long I/O must poll; BOOT polled during blocking work |
| `#one-field-table-two-portals` | `core::portal` is the single field table; device renders WiFiManagerParameters, native renders HTML |
| `#generated-sources` | → `docs/constraints.md#generated-sources` |
| `#tests` | Env map: `native_test`, `native_test_fetch`, `native_test_png`, `native_test_live` |
| `#docs-map` | Where each kind of knowledge lives, and the pointer convention |

### `docs/modules/terrain.md`

| Anchor | Content |
|---|---|
| `#why-tiles` | 65,536 samples/tile vs hundreds of billed point queries; old path was 7 rate-limited requests 12 s apart, ~1.5 min/view, HTTP 429 |
| `#tile-source` | AWS Open Data `elevation-tiles-prod/terrarium`; no key, no rate limit; `height_m = R*256 + G + B/256 - 32768`; bathymetry included |
| `#https-is-mandatory` | Bucket policy requires `aws:SecureTransport`, plain HTTP → 403; hence TLS cost, hence borrowed scratch |
| `#grid-geometry` | Radar's own flat-earth convention (1° ≈ 111 km on **both** axes, like `core::geo`) so a grid point projects where the overlay samples it, distortion included. Row 0 = north, col 0 = west |
| `#grid-size` | 41/side: ~6 px/sample on a 240 px disc, `41*41*2` = 3.4 KB; **odd on purpose** so the middle point lands on the radar centre |
| `#zoom-selection` | Largest zoom whose bbox is ≤ 256 px on both axes ⇒ ≤ 2×2 block ⇒ ≤ 4 requests; ordering checks reject a wrapped box, falling through to zoom 0 (one tile, whole world, coarse but correct) |
| `#mercator-clamp` | Latitude clamped to ±85.05112878; `nextafter` clamp on `py` because at the clamp latitude the closed form lands a fraction of a pixel outside, which would have `floor()` name a nonexistent tile row and drop a high-Arctic view's rows |
| `#antimeridian` | Longitude **deliberately not clamped**: a crossing view depends on `px` running past the edge so `tilesForView()` can wrap it with a modulo. `beginTile()` takes the x offset modulo world width for the same reason |
| `#axis-separable-resampler` | Longitude drives x and latitude y only, so 1,681 sample positions collapse to two axes (`s_col_px`, `s_row_py`); one diagonal walk fills both; both non-decreasing |
| `#tile-local-coordinates` | `s_col_local`/`s_row_local`, −1 for absent; tile-local is what makes the hot loop correct across the antimeridian |
| `#reverse-map` | `s_row_first`/`s_col_first`: first grid row/col sampling each tile pixel. Only the first, because equal entries are contiguous. Matters at high latitude where Mercator squeezes two grid columns onto one pixel — a single index would leave the other at sea level |
| `#per-pixel-sink` | `onPixel` runs up to 65,536×/tile; the early-out on unsampled rows (5 of 6 at our zooms) carries the cost |
| `#incremental-download` | One tile per `ensureGrid()` call, blocking for that request while polling, spaced `kTerrainTileIntervalMs`; a view change mid-download restarts it; single cursor suffices |
| `#retry-policy` | Failed tile retried in place without discarding decoded tiles; `kMaxTileFailures = 3` then the retry gate; gate keyed by preset so tapping to another range tries immediately |
| `#discard-partial-grids` | Every sample sits inside a requested tile, so a short count means tile selection and sample mapping disagree. Gaps would paint as a hard sea-level band that **reads as real terrain** — leaving the layer off is the honest failure |
| `#elevation-saturation` | Encoding ceiling 32767.996 m, half a metre past `int16_t`. Everest encodes as R=162; saturating makes a corrupt pixel read as a peak instead of wrapping to a −32768 m hole |
| `#center-epsilon` | 1e-7° ≈ 1 cm, far below a tile pixel: absorbs float noise, never a real relocation |
| `#repaint-after-decode` | → `docs/constraints.md#borrowed-scratch` |
| `#one-grid-cached` | → `docs/constraints.md#one-terrain-grid` |

### `docs/modules/png-decode.md`

| Anchor | Content |
|---|---|
| `#why-our-own-decoder` | Not pngle. → `constraints.md#borrowed-scratch`, `#freeing-the-sprite-fails` |
| `#scratch-layout` | → `constraints.md#borrowed-scratch` (arithmetic table lives there) |
| `#lives-in-platform` | `core/` must not include LovyanGFX; `main.cpp` wires `decode()` into `setPngDecoder()` and points `setScratch()` at the sprite |
| `#accepted-inputs` | 8-bit truecolour RGB, non-interlaced, ≤ 256 px wide — what terrain tiles are. Anything else **refused rather than guessed at** |
| `#streaming` | Only the window and two scanlines held; the raster never exists in memory, so tile size costs nothing |
| `#canonical-huffman-tables` | `count[n]` codes of length n + `symbol[]` in code order; decoding walks lengths and needs no tree, which is what keeps the tables small enough for borrowed memory (RFC 1951 §3.2.2) |
| `#code-length-table-aliasing` | The code-length alphabet's table is transient and **borrows the literal table's storage**; safe only because real literal lengths are decoded after it |
| `#idat-framing` | Real tiles split compressed data across several IDATs at arbitrary points — a boundary can fall mid-scanline, mid-Huffman-code. `IdatReader` hides chunk headers and CRCs from the inflate. A non-IDAT chunk means the image data is over |
| `#byte-boundary-realignment` | Stored-block length and the Adler trailer are byte-aligned, but **only the partial bits may be dropped** — whole buffered bytes are still part of the stream |
| `#adler-32` | The only end-to-end check that the bytes handed to the caller are the bytes the encoder compressed. Everything else validates *structure*, and a stream can be structurally perfect while inflating to wrong data. Without it a corrupt tile becomes terrain that looks entirely plausible. Sums reduced every 5,552 bytes — the longest run zlib proves cannot overflow 32 bits — turning a division per byte into one per 5,552. Trailer is big-endian, unlike DEFLATE |
| `#bounds-are-a-contract` | A stream inflating past the declared height is refused in `emit()`, **not afterwards in `run()`**: one more full scanline reaches `emitRow()` with `y == height_`, and sinks are entitled to trust the bounds `PixelFn` promises. A well-formed stream never trips it |
| `#unverified-pass` | Last scanline before the final block ⇒ what follows is more DEFLATE, not the trailer, so nothing to compare yet. Every row decoded, so it is a pass — just an unverified one |
| `#no-scratch` | The sprite is the lender, so `nullptr` means it could not be created: the radar is already painting straight to the panel and terrain is the least of the problems |

### `docs/modules/render.md`

| Anchor | Content |
|---|---|
| `#frame-sprite` | Composite grid **and** aircraft into the off-screen sprite, then one `pushSprite`. Panel updates in one pass, so labels never show an erase/redraw gap — no flicker |
| `#draw-paths` | `radarDisplayDraw()` vs `radarDisplayRefreshAircraft()`; both go through `renderFrame()` |
| `#direct-to-panel-fallback` | Sprite alloc failure ⇒ draw straight to the panel with visible progressive redraw. Also what you are looking at if `#freeing-the-sprite-fails` ever happens again |
| `#lent-scratch-contract` | → `constraints.md#borrowed-scratch`. Contents come back as garbage; caller must repaint |
| `#guard-statics` | `s_frame_ready`, `s_label_metrics_ready`, `s_tag_label_metrics_ready`, `s_vlw_loaded` → `constraints.md#reboot-is-process-exit` |
| `#fixed-point-upsample` | → `constraints.md#no-fpu` |
| `#pixel-to-grid-map` | Mapping identical on both axes and every frame (px 0 → grid line 0, px `kSize-1` → grid line `kGrid-1`), so cell index + weight computed once. Last pixel lands exactly on the final grid line, so the cell index is held one short with full weight, keeping `cell+1` in range |
| `#row-blend-then-lerp` | Row weight constant across a scanline, so blend the two bracketing grid rows into one `kGrid`-entry row up front and per-pixel work is one horizontal lerp. Whole metres keep both lerps inside `int32` for any terrarium pair |
| `#scanline-run-coalescing` | Neighbours almost always share a band, so runs become one `drawFastHLine` instead of 240 `drawPixel`. Band −1 (water / below the first floor) draws nothing so the background fill stays visible |
| `#hypsometric-bands` | Dark lowlands → lighter highlands; below `kTerrainBandMinM[0]` keeps the plain background, which is also how water stays unpainted (tiles carry bathymetry, so sea reads as negative elevation, not no-data). Deliberately dimmer than the grid green so rings, runways and aircraft stay legible on top |
| `#grid-axis-alignment` | Grid row 0 = north, col 0 = west, matching screen y/x, so scanlines sample without an axis flip |
| `#tag-compaction` | Above `kTagCompactAboveCount = 4` tagged aircraft, tags collapse to callsign alone: three lines is fine for a handful, unreadable mush once busy, and the callsign is the line you actually read. **Only aircraft inside the outer ring count** — rim dots carry no tag, so counting them would collapse readable tags because of traffic not even on the disc |
| `#tag-placement` | Placed toward the centre: west (left) aircraft get tags on the right, east on the left |
| `#speed-vector-scale` | Fixed screen scale — 60 s horizon at ground speed against `kAircraftTrackRefOuterKm`, not the active preset — so vector length is comparable across zooms |
| `#runway-exaggeration` | Real runways 2–4 km; at a 20 NM ring that is a couple of pixels and orientation — the useful part — is invisible. Stretched ×5 about the midpoint **in degrees, before projection**, so position and bearing stay true and exaggeration scales with the preset. Deliberately cosmetic: an orientation aid, not a chart |
| `#font-metric-derivation` | Layout derived at runtime from VLW metrics by binary search; the native harness mirrors the search exactly. → `docs/fidelity-baseline.txt` |
| `#draw-into-the-sprite` | Status screens draw anti-aliased circles straight to the panel and the device cannot read back (`pin_miso = -1`), so it blends against assumed black. **New UI work must draw into the sprite; direct-to-panel anti-aliasing is a defect** |
| `#panel-colour-order` | GC9A01 BGR panel: `initPalette()` software-swaps R/B for the aircraft colour only, gated on `config::kDisplayRgbOrder`. → open TODO, §8 |
| `#fetch-radius-vs-ring` | `fetchRadiusKm()` scales to the screen edge, not the outer ring, so rim-dot targets have data; `terrainHalfSpanKm()` covers the whole square frame |

### `docs/modules/native-harness.md` *(new — see §1)*

Anchors: `#why-not-an-emulator`, `#main-thread` (→ constraints), `#boot-button-on-the-keyboard`, `#simulated-radio`, `#reentrancy-guard`, `#settings-json`, `#curl-backend`, `#timeout-cap`, `#tls-verification-stays-on`, `#font-blob-lifetime` (→ constraints), `#fidelity`, `#frame-hash`.

### `docs/modules/portal.md` *(new — see §1)*

Anchors: `#one-field-table`, `#wifimanager-checkbox-quirk`, `#attribute-buffer-ownership`, `#staged-latlon-commit`, `#absent-checkbox-pass`, `#loopback-only`, `#single-threaded-pump`, `#form-urlencoded-decode`, `#ssid-is-not-in-the-table`, `#force-portal-flag`.

## 5. The migration table

**How to execute a row.** Delete the comment at `file:lines`. If a pointer is given, insert exactly that one line in its place, at the same indentation, immediately above the declaration or statement it described. `— (delete)` means delete with no replacement. `KEEP INLINE` means do not delete; see §7.

Pointer style: `// See docs/constraints.md#borrowed-scratch`. `ARCHITECTURE.md` is at the repo root, so its pointers have no `docs/` prefix.

### 5.1 `include/config.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-9 | Portable config; free of Arduino/ESP-IDF; device pins in pins.h | `constraints.md#layering` | `// Portable — no Arduino/ESP-IDF headers. See docs/constraints.md#layering` |
| 15, 32, 39, 53, 57, 98 | Banners | — | — (delete) |
| 18, 22, 25, 27, 29, 34, 36, 88, 93, 95 | Units / valid ranges / format of each constant | **stay** | KEEP INLINE |
| 43-50 | `kDisplayRgbOrder` pending on-hardware colour check; read by both LGFX config and `initPalette()`; swap deleted + constant moves to pins.h once confirmed | `render.md#panel-colour-order` | `// TODO(step 1): pending on-hardware colour check. See docs/modules/render.md#panel-colour-order` (keep as ≤3-line TODO, §8) |
| 58-65 | 41 grid points: ~6 px/sample, 3.4 KB, exactly one cached, odd so the middle lands on centre | `terrain.md#grid-size` | `// See docs/modules/terrain.md#grid-size` |
| 67-78 | AWS terrarium bucket; elevation formula; **https mandatory** (403 on plain http) → ~30 KB TLS → streaming decode → borrowed scratch | `terrain.md#tile-source`, `#https-is-mandatory` | `// See docs/modules/terrain.md#tile-source and #https-is-mandatory` |
| 82-86 | Pause exists only to hand control back to the main loop; source unthrottled; whole grid in ~1 s | `terrain.md#incremental-download` | `// See docs/modules/terrain.md#incremental-download` |
| 91 | adsb.fi public limit 1 req/s | `ARCHITECTURE.md#main-loop-contract` | `// adsb.fi public limit is 1 req/s.` (KEEP INLINE — external contract) |

### 5.2 `include/core/platform.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-12 | The portability seam; one impl per destination; no Arduino/ESP-IDF/LovyanGFX | `ARCHITECTURE.md#portability-seam` | `// The portability seam. See ARCHITECTURE.md#portability-seam` |
| 20, 25, 33, 38, 53, 65, 75, 103 | Banners | — | — (delete) |
| 22, 27, 30, 35, 67, 69-71, 126, 129, 133, 169-172 | Contracts: monotonic clock, real sleep not spin, printf semantics, field meanings, `read()` returns −1, short count = EOF | **stay** | KEEP INLINE |
| 40-50 | `reboot()`: native `exit(0)` not re-entry; guard statics would survive; reached from a nested chain that must not recurse | `constraints.md#reboot-is-process-exit` | `// Native is exit(0), not re-entry. See docs/constraints.md#reboot-is-process-exit` — **KEEP INLINE, §7** |
| 55-61 | `loadFont()` RETAINS the pointer; storage must outlive the program | `constraints.md#font-blob-lifetime` | `// loadFont() retains this pointer — storage must be immortal. See docs/constraints.md#font-blob-lifetime` — **KEEP INLINE, §7** |
| 77-85 | Namespace is a call parameter and every op opens/closes its own handle, both deliberate; separate namespaces avoid handle conflicts | `constraints.md#frozen-storage-keys` | `// See docs/constraints.md#frozen-storage-keys` |
| 105-111 | `PollFn` wired to `wifiLoop()`; dropping it leaves the portal dead for every fetch | `ARCHITECTURE.md#main-loop-contract` | `// See ARCHITECTURE.md#main-loop-contract` |
| 114-122 | Body never held whole; a single allocation that size throws `std::bad_alloc` and aborts the firmware; signatures match ArduinoJson | `constraints.md#no-whole-body-buffering` | `// Never buffer a whole body. See docs/constraints.md#no-whole-body-buffering` — **KEEP INLINE, §7** |
| 160-165 | `BodyFn` runs with the connection open; must not block past the timeout | `ARCHITECTURE.md#portability-seam` | Keep the timing sentence as API CONTRACT; **KEEP INLINE** |

### 5.3 `include/core/terrain.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-19 | Why tiles not point queries; streaming through `PngDecodeFn`; decoder borrows the sprite; flat-earth grid convention; row 0 north, col 0 west | `terrain.md#why-tiles`, `#grid-geometry` | `// See docs/modules/terrain.md#why-tiles and #grid-geometry` |
| 32, 35, 38, 41, 46, 47, 50, 57 | Tile size, `kMaxTiles` guarantee, zoom range, field units, row-major/north-edge | **stay** | KEEP INLINE |
| 61-67 | Injected so `core/` stays LovyanGFX-free; host tests supply their own; without it `ensureGrid()` does nothing | `ARCHITECTURE.md#injected-seams` | `// Injected so core/ stays LovyanGFX-free. See ARCHITECTURE.md#injected-seams` |
| 72, 75, 78, 81, 102 | Contracts of `setPollFn`, `clear`, `downloadActive`, `gridReady`, `grid` | **stay** | KEEP INLINE |
| 84-98 | `ensureGrid()` incrementality, one tile per call, retry in place, retry gate, discard partial grids, one grid cached | `terrain.md#incremental-download`, `#retry-policy`, `#discard-partial-grids`, `#one-grid-cached` | Keep the 3-line behavioural contract (one tile per call; true only when complete; cheap every loop), then `// Retry, discard and cache policy: docs/modules/terrain.md#retry-policy` |
| 105 | Banner | — | — (delete) |
| 107-110, 114, 117-122, 124-130, 132-135, 141-147 | Helper contracts: row/col orientation, terrarium formula, Mercator space + clamp, zoom rule, raster order, band index rule | **stay** | KEEP INLINE. Add `// Mercator edge cases: docs/modules/terrain.md#mercator-clamp` above `latLonToTilePixel` and `// docs/modules/terrain.md#zoom-selection` above `zoomForView` |

### 5.4 `include/platform/png_decode.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-25 | The whole memory argument: 115 KB + 30 KB TLS + decoder = 13 KB over; freeing fails (114,676 / 115,200, TIME_WAIT); borrowing dissolves it; lives in `platform/` because `core/` must not include LovyanGFX | `constraints.md#borrowed-scratch`, `#freeing-the-sprite-fails`; `png-decode.md#why-our-own-decoder`, `#lives-in-platform` | `// Own inflate, allocates nothing, decodes into lent scratch.`<br>`// See docs/constraints.md#borrowed-scratch and #freeing-the-sprite-fails` |
| 35-43 | Scratch arithmetic: 32768 window (RFC 1951) + 2048 two scanlines + 1024 Huffman tables; the 115,200 sprite has room | `constraints.md#borrowed-scratch` | **KEEP INLINE (3 lines of arithmetic), §7**, plus `// See docs/constraints.md#borrowed-scratch` |
| 46-50 | Called once per image; scratch need only be untouched until that image is decoded; nothing retained between calls | **stay** | KEEP INLINE |
| 54-62 | Raster order; only window + two scanlines held so tile size costs nothing; accepted inputs; returns false on malformed/truncated/no-scratch | **stay** + `png-decode.md#accepted-inputs`, `#streaming` | KEEP the contract; replace the "why" sentence with `// See docs/modules/png-decode.md#accepted-inputs` |

### 5.5 `include/ui/radar_display.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 8, 11 | Draw-path contracts | **stay** | KEEP INLINE |
| 14-25 | Lends the sprite's pixels; why lending beats freeing; nobody composites during a decode; **contents come back as garbage so the caller must repaint** | `constraints.md#borrowed-scratch`; `render.md#lent-scratch-contract` | `// INVARIANT: no frame may be composed while this scratch is on loan; contents`<br>`// come back as garbage and the caller must repaint. docs/constraints.md#borrowed-scratch` — **KEEP INLINE, §7** |

### 5.6 `include/core/settings.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-14 | Merged from former `services/radar_location.cpp` + `ui/radar_range.cpp`; storage via `KeyValueStore`; two NVS namespaces deliberately not merged | `ARCHITECTURE.md#layer-map`; `constraints.md#frozen-storage-keys` | `// Persistent user settings. See docs/constraints.md#frozen-storage-keys` (drop the merge history — archaeology, not a constraint) |
| 21-31 | Presets authored in NM (aviation's natural unit), converted to km because downstream projection is metric; the four values | `render.md#fetch-radius-vs-ring` | `// Authored in NM, stored in km — downstream projection is metric.` (KEEP INLINE, 1 line) |
| 33, 41, 63, 64, 70, 78, 81, 96, 103, 108, 124, 127, 140, 143 | Units / key lists / contracts | **stay** | KEEP INLINE |
| 57-62 | Namespaces stay separate (NVS handle conflicts); **key names frozen** — changing either strands configured devices | `constraints.md#frozen-storage-keys` | `// Namespaces and key names are frozen. See docs/constraints.md#frozen-storage-keys` — **KEEP INLINE, §7** |
| 68, 73, 84, 94, 101, 122 | Banners | — | — (delete) |
| 113-119 | `unitsReset()` deliberately does not reset the range preset | `constraints.md#frozen-storage-keys` | `// Deliberately does NOT reset the range preset. See docs/constraints.md#frozen-storage-keys` |
| 130-137 | WiFiManager submits `value=` not "on"; portal prefills "T"/"F"; real state in `checked`; so any single T/F means checked | `portal.md#wifimanager-checkbox-quirk` | `// See docs/modules/portal.md#wifimanager-checkbox-quirk` |

### 5.7 `include/core/geo.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-10 | Extracted from `radar_display.cpp`'s anonymous namespace; explicit `Viewport` makes it pure and host-testable | `ARCHITECTURE.md#layer-map` | `// Pure projection over an explicit Viewport. See ARCHITECTURE.md#layer-map` |
| 14 | Flat-earth approximation, consistent with the original | `terrain.md#grid-geometry` | `// Flat-earth: 1 deg ~ 111 km on BOTH axes. See docs/modules/terrain.md#grid-geometry` |
| 17, 21-24, 32, 41, 44-49, 54, 57-63, 67-72 | Field units, projection contract, `inset_px` meaning, rim-point contract, clip behaviour | **stay** | KEEP INLINE |

### 5.8 `include/core/portal_params.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-9 | One field table, both destinations; keeping one table is what stops the forms drifting | `portal.md#one-field-table` | `// See docs/modules/portal.md#one-field-table` |
| 16-18, 22-27, 53, 56, 59 | `Kind` meanings, `Field` field docs, buffer size hint, error behaviour | **stay** | KEEP INLINE |
| 33-39 | Checkboxes always return "T"; state carried by `checked` from `fieldHtmlAttrs()` | `portal.md#wifimanager-checkbox-quirk` | KEEP the contract line; add `// See docs/modules/portal.md#wifimanager-checkbox-quirk` |
| 42-50 | Writes into caller-owned buffer because `WiFiManagerParameter` keeps `custom` as a pointer; each field needs its own buffer outliving the portal; refresh must rewrite in place | `portal.md#attribute-buffer-ownership` | `// Caller-owned buffer: WiFiManagerParameter retains this pointer.`<br>`// See docs/modules/portal.md#attribute-buffer-ownership` — **KEEP INLINE, §7** |
| 62-68 | lat/lon only meaningful as a pair, staged during apply and persisted here | `portal.md#staged-latlon-commit` | `// lat/lon are staged during apply and persisted here as a pair.`<br>`// See docs/modules/portal.md#staged-latlon-commit` |

### 5.9 `include/ui/radar_theme.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 11, 14, 16, 19, 22, 24, 29, 34, 38, 40, 42, 44, 46, 66, 68, 72, 75, 91, 113, 127 | Units, targets, geometry meanings | **stay** | KEEP INLINE |
| 49-59 | Runway exaggeration: real runways 2–4 km read as dots at 20 NM; stretched about the midpoint so position and bearing stay true; deliberately cosmetic (3 km draws as 15 km) | `render.md#runway-exaggeration` | `// See docs/modules/render.md#runway-exaggeration` |
| 78-88 | Tag compaction: three lines is mush once busy, callsign is the line you read; counts only aircraft inside the ring | `render.md#tag-compaction` | `// See docs/modules/render.md#tag-compaction` |
| 118-125 | Hypsometric bands; below the first floor keeps the background, which is how water stays unpainted (bathymetry ⇒ sea reads as negative); dimmer than grid green so overlays stay legible | `render.md#hypsometric-bands` | `// See docs/modules/render.md#hypsometric-bands` |

### 5.10 `include/ui/radar_range.h`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 3-10 | Forwarder onto `core::settings`; `fetchRadiusKm()` stays here because it is screen geometry and depends on `radar_theme.h`, which `core/` deliberately does not | `ARCHITECTURE.md#layer-map` | `// UI-layer forwarders onto core::settings. See ARCHITECTURE.md#layer-map` |
| 22-27 | **`static` is load-bearing**: a constexpr reference at namespace scope has external linkage ⇒ multiple-definition link error; `static` rather than `inline` because parts of the ESP32 Arduino build compile below C++17 | `constraints.md#layering` | `// 'static' is load-bearing: without internal linkage every TU emits a definition`<br>`// and the link fails. Not 'inline' — parts of the Arduino build are pre-C++17.` — **KEEP INLINE, §7** |
| 59-62, 70-74 | Why the fetch radius scales to the screen edge, not the ring; what `terrainHalfSpanKm()` covers | **stay** + `render.md#fetch-radius-vs-ring` | KEEP INLINE |

### 5.11 Remaining `include/` files

| File:lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| `core/adsb.h:3-8` | Networking via `core::platform::HttpClient`; device HTTPClient, native libcurl | `ARCHITECTURE.md#portability-seam` | `// See ARCHITECTURE.md#portability-seam` |
| `core/adsb.h:20-23` | Poll hook keeps portal + BOOT alive across a request | `ARCHITECTURE.md#main-loop-contract` | `// See ARCHITECTURE.md#main-loop-contract` |
| `core/adsb.h:26,29,32,35` | Contracts | **stay** | KEEP INLINE |
| `core/aircraft.h:7-12` | Tag strings fixed-size and pre-formatted at parse time so the render path does no allocation and no formatting | `constraints.md#no-whole-body-buffering` | `// Pre-formatted at parse time: the render path allocates and formats nothing.` (KEEP INLINE, 1 line) |
| `core/aircraft.h:16,17,24` | Field meanings; hard cap and why (render path allocates arrays of this size) | **stay** | KEEP INLINE |
| `core/airport_find.h:7` | Lookup contract | **stay** | KEEP INLINE |
| `core/large_airports.h:1` | Generated — do not edit | `constraints.md#generated-sources` | **KEEP INLINE verbatim, §7** |
| `core/tap_gesture.h:11,14,17` | Contracts | **stay** | KEEP INLINE |
| `platform/device/lgfx_config_device.hpp:9` | Pin values come from `pins.h` | **stay** | KEEP INLINE |
| `platform/device/pins.h:3-9` | Split out of config.h because it pulls `<driver/gpio.h>`; device build only | `constraints.md#layering` | `// Device-only: pulls <driver/gpio.h>. See docs/constraints.md#layering` |
| `platform/device/pins.h:17,20` | Banners | — | — (delete) |
| `platform/device/pins.h:23-24` | GPIO2 is a strapping pin, must not be held LOW at reset; safe only because the panel's DC input is high-impedance | `constraints.md#gpio2-strapping` | `// GPIO2 is a strapping pin — must not be held LOW at reset. docs/constraints.md#gpio2-strapping` — **KEEP INLINE, §7** |
| `platform/device/pins.h:26,27` | SDA/SCL naming | **stay** | KEEP INLINE |
| `platform/device/pins.h:30` | GC9A01 modules often need invert for correct black/green | `render.md#panel-colour-order` | `// See docs/modules/render.md#panel-colour-order` |
| `platform/native/lgfx_config_native.hpp:3-16` | Declares no class of its own; `LGFX_AutoDetect_sdl.hpp` already defines `lgfx::LGFX` and `using LGFX`, so our own class would be a hard redefinition | `native-harness.md#why-not-an-emulator` | `// Deliberately declares no class: LGFX_AutoDetect_sdl.hpp already defines LGFX.`<br>`// See docs/modules/native-harness.md#why-not-an-emulator` |
| `platform/wifi_setup.h:3-10` | `wifiIsConnected()` deliberately equivalent to `WL_CONNECTED` and nothing more; the stricter `wifiLinkUp()` also needs a non-zero IP; substituting it would change loop() timing | `native-harness.md#simulated-radio` | `// Deliberately just WL_CONNECTED. Substituting the stricter wifiLinkUp() would`<br>`// change the WiFi-lost / grace / reconnect timing in loop(). Keep them distinct.` — **KEEP INLINE, §7** |
| `platform/wifi_setup.h:13,16,18,20,23,25,27` | Contracts | **stay** | KEEP INLINE |
| `ui/display.h:3-10` | Both destinations expose the same `tft` and `displayInit()`; **the single sanctioned #ifdef** in shared code | `constraints.md#layering` | `// The single sanctioned #ifdef in shared code. See docs/constraints.md#layering` — **KEEP INLINE, §7** |
| `ui/display_font.h:8,11,14` | Contracts | **stay** | KEEP INLINE |
| `ui/status_screens.h:7` | Call Tick until connect finishes | **stay** | KEEP INLINE |
| `ui/terrain_overlay.h:7-13` | Draws nothing when toggled off or no grid; **call right after the background fill and before the grid rings** | **stay** (ordering requirement) | KEEP INLINE |

### 5.12 `src/core/terrain.cpp`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 14-18 | 1e-7° ≈ 1 cm, below a tile pixel: absorbs float noise, never a real relocation | `terrain.md#center-epsilon` | `// See docs/modules/terrain.md#center-epsilon` |
| 21 | Mercator undefined at the poles; conventional cut-off | `terrain.md#mercator-clamp` | `// See docs/modules/terrain.md#mercator-clamp` |
| 26 | Sentinel meaning | **stay** | KEEP INLINE |
| 29-37 | **ONE cached grid, not one per preset**: 4 slots = 13 KB `.bss`; heap holds ~45 KB with the sprite live and TLS needs ~30 KB, so those KB decide whether the ADS-B socket opens; re-fetch costs 1–4 tiles / ~1 s | `constraints.md#one-terrain-grid` | `// Exactly ONE slot. A per-preset cache costs ~10 KB of .bss that the TLS`<br>`// session needs. See docs/constraints.md#one-terrain-grid` — **KEEP INLINE, §7** |
| 41-44 | Retry gate keyed by preset so tapping to another range tries immediately | `terrain.md#retry-policy` | `// See docs/modules/terrain.md#retry-policy` |
| 51-54 | One download at a time; single cursor suffices; a view change mid-download restarts it | `terrain.md#incremental-download` | `// See docs/modules/terrain.md#incremental-download` |
| 64-67 | Field meanings | **stay** | KEEP INLINE |
| 70 | Attempts before abandoning | **stay** | KEEP INLINE |
| 74-82 | Longitude drives x and latitude y only, so 1,681 positions collapse into two axes — the whole resampler is built on that; both non-decreasing | `terrain.md#axis-separable-resampler` | `// See docs/modules/terrain.md#axis-separable-resampler` |
| 86-91 | Tile-local coordinates, −1 for absent; tile-local is what makes it correct across the antimeridian | `terrain.md#tile-local-coordinates` | `// See docs/modules/terrain.md#tile-local-coordinates` |
| 95-104 | Reverse map; only the FIRST index because equal entries are contiguous; matters at high latitude where two grid columns share one pixel | `terrain.md#reverse-map` | `// See docs/modules/terrain.md#reverse-map` |
| 108 | Parked here because `PixelFn` carries no capture | `ARCHITECTURE.md#injected-seams` | `// PixelFn carries no capture. See ARCHITECTURE.md#injected-seams` |
| 120 | NW/SE corners = bounding box | RESTATES | — (delete) |
| 128 | Pins sample positions to the pixel grid | RESTATES | — (delete) |
| 136-137 | The two axes are independent, so one diagonal walk fills both tables | `terrain.md#axis-separable-resampler` | `// One diagonal walk fills both tables. docs/modules/terrain.md#axis-separable-resampler` |
| 145 | See `s_row_first` | RESTATES (cross-ref) | — (delete) |
| 152-155 | x offset modulo world width because `tilesForView()` wraps at the antimeridian; a plain subtraction would place those columns nowhere and leave them at sea level | `terrain.md#antimeridian` | `// Modulo the world width — see docs/modules/terrain.md#antimeridian` |
| 180-185 | Runs up to 65,536×/tile; the early-out on unsampled rows (5 of 6) carries the cost | `terrain.md#per-pixel-sink` | `// See docs/modules/terrain.md#per-pixel-sink` |
| 218-222 | Decoder holds no memory of its own; nothing to hand back, but **the caller must repaint — those borrowed pixels were the frame** | `constraints.md#borrowed-scratch` | `// The caller must repaint: those borrowed pixels were the frame.`<br>`// See docs/constraints.md#borrowed-scratch` — **KEEP INLINE, §7** |
| 225 | Holds off retries | RESTATES | — (delete) |
| 271-274 | Encoding ceiling is 32767.996 m; Everest encodes as R=162; saturating makes a corrupt pixel a peak instead of a −32768 m hole | `terrain.md#elevation-saturation` | `// See docs/modules/terrain.md#elevation-saturation` |
| 291 | `atanh(sin lat)` is the numerically friendly form of `ln(tan + sec)` | `terrain.md#mercator-clamp` | `// atanh(sin lat) — the numerically friendly form of ln(tan + sec).` (KEEP INLINE, 1 line) |
| 295-299 | Clamp latitude with `nextafter` or `floor()` names a nonexistent tile row and drops a high-Arctic view's rows; **longitude deliberately NOT clamped** so `tilesForView()` can wrap px with a modulo | `terrain.md#mercator-clamp`, `#antimeridian` | `// Latitude clamped, longitude deliberately NOT — see`<br>`// docs/modules/terrain.md#mercator-clamp and #antimeridian` — **KEEP INLINE, §7** |
| 324-326 | ≤1-tile span crosses ≤1 boundary per axis ⇒ 2×2 block; ordering checks reject a wrapped box, which falls through to zoom 0 | `terrain.md#zoom-selection` | `// See docs/modules/terrain.md#zoom-selection` |
| 363 | Above north / below south edge of the projection | `terrain.md#mercator-clamp` | `// outside the projection` (shorten) |
| 370 | Longitude wraps | `terrain.md#antimeridian` | `// longitude wraps` (KEEP INLINE) |
| 409 | First tile goes out immediately | **stay** | KEEP INLINE |
| 415-417 | Only a view outside the projection's latitude range gets here; take the gate so it is diagnosed once | `terrain.md#retry-policy` | `// Only an out-of-projection view reaches here. docs/modules/terrain.md#retry-policy` |
| 425 | Hand control back to the main loop between tiles | `terrain.md#incremental-download` | `// See docs/modules/terrain.md#incremental-download` |
| 443-445 | A single failed tile must not scrap decoded ones; only repeated failure abandons to the gate | `terrain.md#retry-policy` | `// See docs/modules/terrain.md#retry-policy` |
| 460 | More tiles to go | RESTATES | — (delete) |
| 465-468 | Short count means tile selection and sample mapping disagree; publishing would paint gaps as a hard sea-level band that **reads as real terrain**; leaving the layer off is the honest failure | `terrain.md#discard-partial-grids` | `// Gaps would read as real terrain. See docs/modules/terrain.md#discard-partial-grids` — **KEEP INLINE, §7** |

### 5.13 `src/platform/png_decode.cpp`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 11 | Tiles are 256 px; the window is what makes anything wider pointless | `png-decode.md#accepted-inputs` | `// See docs/modules/png-decode.md#accepted-inputs` |
| 13, 16, 19, 23, 58, 65, 74 | Format facts with RFC citations (no alpha; RFC 1951 fixes the window; Adler modulus + 5552 run; alphabet sizes; length/distance bases; code-length order) | **stay** (spec citations) | KEEP INLINE |
| 29-34 | Canonical Huffman: `count`/`symbol`, no tree, which is what keeps this small enough for borrowed memory | `png-decode.md#canonical-huffman-tables` | `// See docs/modules/png-decode.md#canonical-huffman-tables` |
| 40-44 | Everything laid over the caller's scratch in one piece; sized by the `static_assert` so the header's promise is compiler-checked | `constraints.md#borrowed-scratch` | `// Laid over the caller's scratch; sized by the static_assert below.`<br>`// See docs/constraints.md#borrowed-scratch` — **KEEP INLINE, §7** |
| 53-54 | `static_assert` message | **CODE, not a comment** | Do not touch |
| 89-94 | `IdatReader` hides chunk headers/CRCs; real tiles split at arbitrary points, so a boundary can fall mid-scanline, mid-Huffman-code | `png-decode.md#idat-framing` | `// See docs/modules/png-decode.md#idat-framing` |
| 99, 115-118, 186 | Return contracts; "returns false rather than guessing" | **stay** | KEEP INLINE |
| 157, 179 | `// CRC`, `// ancillary chunk and its CRC` | **stay** (explain magic constants) | KEEP INLINE |
| 202-204 | Anything not IDAT means image data is over: IEND or a trailing ancillary chunk | `png-decode.md#idat-framing` | `// Non-IDAT means the image data is over. docs/modules/png-decode.md#idat-framing` |
| 243-247 | Inflate output consumed a byte at a time by the unfilter, so no raster and no decompressed copy is held; both stages share one `Work` | `png-decode.md#streaming` | `// See docs/modules/png-decode.md#streaming` |
| 302-306 | Last scanline before the final block ⇒ nothing to compare yet; a pass, just unverified | `png-decode.md#unverified-pass` | `// See docs/modules/png-decode.md#unverified-pass` |
| 312, 344, 470, 544, 591 | Banners | — | — (delete) |
| 337 | Preset dictionary: never used by PNG | **stay** | KEEP INLINE |
| 346, 366 | Contracts | **stay** | KEEP INLINE |
| 426-427 | The code-length table **borrows the literal table's storage**; safe because the real literal lengths are only decoded after it | `png-decode.md#code-length-table-aliasing` | `// Aliases the literal table's storage. docs/modules/png-decode.md#code-length-table-aliasing` — **KEEP INLINE, §7** |
| 448 | Nothing to repeat | **stay** | KEEP INLINE |
| 473-474 | A stored block's length starts on a byte boundary, so **only the partial bits are dropped** — whole buffered bytes are still part of the stream | `png-decode.md#byte-boundary-realignment` | `// Only the partial bits. docs/modules/png-decode.md#byte-boundary-realignment` — **KEEP INLINE, §7** |
| 546-550 | One inflated byte goes into the window (a later back-reference may need it) and the scanline; `written_` counts the whole stream so masking gives the circular position | `png-decode.md#streaming` | `// See docs/modules/png-decode.md#streaming` |
| 552-556 | Must be refused **here, not afterwards in `run()`**: one more scanline reaches `emitRow()` with `y == height_`, and sinks are entitled to trust `PixelFn`'s bounds | `png-decode.md#bounds-are-a-contract` | `// Refused here, not in run(): sinks trust PixelFn's bounds.`<br>`// See docs/modules/png-decode.md#bounds-are-a-contract` — **KEEP INLINE, §7** |
| 567 | Every scanline is preceded by its filter type | **stay** | KEEP INLINE |
| 584 | The row just finished is the next row's "above" neighbour | **stay** | KEEP INLINE |
| 593-597 | Sums reduced every 5,552 bytes — longest run zlib proves cannot overflow — turning a division per byte into one per 5,552 | `png-decode.md#adler-32` | `// See docs/modules/png-decode.md#adler-32` |
| 608-614 | **The only end-to-end check** that the bytes handed on are the bytes compressed; everything else validates structure; without it a corrupt tile becomes plausible-looking terrain | `png-decode.md#adler-32` | `// The only end-to-end check the decode has. docs/modules/png-decode.md#adler-32` — **KEEP INLINE, §7** |
| 616-617 | Trailer byte-aligned; only partial bits may be dropped | `png-decode.md#byte-boundary-realignment` | `// docs/modules/png-decode.md#byte-boundary-realignment` |
| 624 | Big-endian, unlike DEFLATE | **stay** | KEEP INLINE |
| 641 | PNG spec §9.2, reverses the filter in place using `row_prev` | **stay** | KEEP INLINE |
| 664 | `0: stored as-is` | **stay** | KEEP INLINE |
| 692-693, 697 | Field meanings; RFC 1950 Adler seeds | **stay** | KEEP INLINE |
| 713-715 | The sprite is the lender, so `nullptr` means it could not be created; the radar is already painting to the panel | `png-decode.md#no-scratch` | `// See docs/modules/png-decode.md#no-scratch` |

### 5.14 `src/ui/terrain_overlay.cpp`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 1-8 | Bilinear upsample of the grid to the full frame; every pixel passes through here, which on a core without an FPU makes this the redraw's hot loop | `constraints.md#no-fpu`; `render.md#fixed-point-upsample` | `// Terrain background: bilinear upsample of the elevation grid to the frame.`<br>`// Every pixel passes through here. See docs/constraints.md#no-fpu` |
| 22-32 | **The measurement**: no FPU ⇒ every float op is a library call; 57,600 px/frame; **209 ms of a 297 ms frame** in float; 8 fractional bits put the error under 1/256 of a cell; elevations are whole metres and bands hundreds of metres apart | `constraints.md#no-fpu` | `// Fixed point, NOT float: no FPU. Floats measured 209 ms of a 297 ms frame here`<br>`// against 37 ms in integers. See docs/constraints.md#no-fpu` — **KEEP INLINE, §7** |
| 36-38 | Mapping identical on both axes and every frame, so cell index + weight are computed once and reused | `render.md#pixel-to-grid-map` | `// See docs/modules/render.md#pixel-to-grid-map` |
| 40 | `0..kFracOne` | **stay** | KEEP INLINE |
| 52-53 | Last pixel lands exactly on the final grid line; keep the cell one short so `cell+1` stays in range with full weight | `render.md#pixel-to-grid-map` | `// Keeps cell+1 in range. docs/modules/render.md#pixel-to-grid-map` |
| 76-79 | Row weight constant across a scanline ⇒ blend the two grid rows up front so per-pixel work is one lerp; whole metres keep both lerps inside int32 for any pair | `render.md#row-blend-then-lerp` | `// See docs/modules/render.md#row-blend-then-lerp` |
| 91-94 | Neighbours almost always share a band ⇒ coalesce runs into one `drawFastHLine` instead of 240 `drawPixel`; band −1 draws nothing so the background stays visible | `render.md#scanline-run-coalescing` | `// See docs/modules/render.md#scanline-run-coalescing` |
| 132-133 | Grid row 0 = north, col 0 = west, matching screen y/x, so no axis flip | `render.md#grid-axis-alignment` | `// See docs/modules/render.md#grid-axis-alignment` |

### 5.15 `src/main.cpp`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 1-7 | Shared by both destinations; everything platform-specific goes through `core::platform` or the `wifi_setup.h` seam | `ARCHITECTURE.md#boot-flow` | `// Shared verbatim by both destinations. See ARCHITECTURE.md#boot-flow` |
| 95-100 | `gridReady()` makes the common case a cheap no-op; `ensureGrid()` rate-limits retries; safe every loop | `terrain.md#incremental-download` | `// See docs/modules/terrain.md#incremental-download` |
| 114-118 | Each decoded tile leaves the sprite full of the decoder's workings ⇒ repaint on the **edge** where a download stops; only the edge, because the retry gate holds it false a minute at a time and repainting every loop would be a full redraw every 10 ms | `constraints.md#borrowed-scratch` | `// Repaint on the download-stop edge: the decode destroyed the frame.`<br>`// Edge only — see docs/constraints.md#borrowed-scratch` — **KEEP INLINE, §7** |
| 152 (no comment today) | The wiring of the lent scratch | `constraints.md#borrowed-scratch` | **ADD** `// See docs/constraints.md#borrowed-scratch` above `platform_png::setScratch(...)` — the agreed exemplar pointer |

### 5.16 `src/ui/radar_display.cpp`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 184 | GC9A01 BGR panel: swap R/B in `color565` so logical red renders red | `render.md#panel-colour-order` | `// See docs/modules/render.md#panel-colour-order` |
| 208 | Current view, rebuilt on demand | **stay** | KEEP INLINE |
| 230 | Fixed screen scale: 60 s horizon at gs, not tied to current range zoom | `render.md#speed-vector-scale` | `// See docs/modules/render.md#speed-vector-scale` |
| 345 | Tag placement rule (west→right, east→left) | `render.md#tag-placement` | `// See docs/modules/render.md#tag-placement` |
| 471-474 | Only tagged aircraft count toward the compact threshold; counting rim dots would collapse readable tags because of traffic not even on the disc | `render.md#tag-compaction` | `// See docs/modules/render.md#tag-compaction` |
| 642-644 | Double-buffered frame: composite grid AND aircraft into the sprite then one `pushSprite`; one pass ⇒ labels never show an erase/redraw gap ⇒ no flicker | `render.md#frame-sprite` | `// See docs/modules/render.md#frame-sprite` |
| 646 | Opens its own `DrawScope` | RESTATES | — (delete) |
| 666 | Fallback when the sprite can't be allocated: draw straight to the panel | `render.md#direct-to-panel-fallback` | `// See docs/modules/render.md#direct-to-panel-fallback` |
| 684-694 (no comment today) | `radarDisplayFrameScratch()` — the lender | `constraints.md#borrowed-scratch` | **ADD** `// INVARIANT: no frame may be composed while this is on loan.`<br>`// See docs/constraints.md#borrowed-scratch` — **§7** |

### 5.17 `src/core/settings.cpp`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 19-23 | **`useKm` is deliberately not the old `useMiles`** — that meant km-vs-statute-miles, this means NM-vs-km, so reusing it would silently invert the preference on every configured device | `constraints.md#frozen-storage-keys` | `// NOT the old "useMiles" key — reusing it would silently invert the preference`<br>`// on every configured device. See docs/constraints.md#frozen-storage-keys` — **KEEP INLINE, §7** |
| 30 | `// 40 NM ring` | **WRONG — index 1 is the 20 NM preset.** | — (delete; do not migrate a false statement) |
| 35 | `// default is nautical miles` | RESTATES (`s_use_km = false`) | — (delete) |
| 142, 166, 207, 292, 303, 339 | Banners | — | — (delete) |
| 336 | `rangeIdx` intentionally left alone; see the header | `constraints.md#frozen-storage-keys` | `// rangeIdx is deliberately kept. See docs/constraints.md#frozen-storage-keys` |

### 5.18 `src/core/adsb.cpp`

| Lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| 33, 43, 154 | Priority order of heading sources; what `exhausted()` distinguishes | **stay** | KEEP INLINE |
| 114-120 | Byte cursor with one character of pushback: the loop reads a delimiter to decide whether another element follows and must hand the first character back before ArduinoJson sees it | `constraints.md#no-whole-body-buffering` | `// One character of pushback — see docs/constraints.md#no-whole-body-buffering` |
| 165-171 | Matches the key, not the first `[`, because the reply also carries "msg"/"now"/"total"; an empty sky answers `"ac":null`, which lands on the ':' branch and reports no array | keep as contract | `// Matches the key, not the first '[': the reply carries other keys, and an`<br>`// empty sky answers "ac":null.` — **KEEP INLINE, §7** |
| 197-205 | Nothing scales with reply size; one aircraft held at a time; **the whole body used to be buffered, which threw `std::bad_alloc` and aborted the firmware** as soon as a wider range pushed the reply past a single heap block | `constraints.md#no-whole-body-buffering` | `// Nothing here scales with reply size. See docs/constraints.md#no-whole-body-buffering` — **KEEP INLINE, §7** |
| 244 | Whatever is left goes unread; the transport closes the connection | **stay** | KEEP INLINE |

### 5.19 Device platform (`src/platform/device/`)

| File:lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| `display_device.cpp:1` | Device display bring-up | **stay** | KEEP INLINE |
| `font_blob_embedded.cpp:1-6` | Embedded by `board_build.embed_files`; storage in flash and immortal, which satisfies `loadFont()`'s retention requirement for free | `constraints.md#font-blob-lifetime` | `// Flash storage is immortal, which satisfies loadFont(). docs/constraints.md#font-blob-lifetime` |
| `http_arduino.cpp:1-10` | Both polling loops exist because a request blocks for seconds and the portal + BOOT must stay alive; TLS deliberately unverified (`setInsecure`), matching shipping firmware | `ARCHITECTURE.md#main-loop-contract`; `native-harness.md#tls-verification-stays-on` | `// Polls so the portal and BOOT survive a request; TLS deliberately unverified.`<br>`// See ARCHITECTURE.md#main-loop-contract` |
| `http_arduino.cpp:51-58` | The window is the only buffer in the path, so peak RAM does not grow with reply size; the poll loop around the refill is load-bearing | `constraints.md#no-whole-body-buffering` | `// See docs/constraints.md#no-whole-body-buffering` |
| `http_arduino.cpp:92` | Blocks until a byte lands, body ends, or time runs out | **stay** | KEEP INLINE |
| `http_arduino.cpp:96-97` | A negative Content-Length means the server sent none; connection close is the only end-of-body marker | **stay** | KEEP INLINE |
| `kv_nvs.cpp:1-8` | A local `Preferences` per call, opened and closed around each operation; avoids the NVS handle conflicts that are why the three namespaces are separate | `constraints.md#frozen-storage-keys` | `// One handle per operation. See docs/constraints.md#frozen-storage-keys` |
| `kv_nvs.cpp:18` | RAII open/close so no path can leak a handle | **stay** | KEEP INLINE |
| `platform_device.cpp:1` | File purpose | **stay** | KEEP INLINE |
| `platform_device.cpp:39` | `esp_restart()` does not return; satisfy `[[noreturn]]` | **stay** | KEEP INLINE |
| `wifi_setup_device.cpp:59` | `wifi` prefs namespace separate from `planeradar` to avoid NVS handle conflicts | `constraints.md#frozen-storage-keys` | `// See docs/constraints.md#frozen-storage-keys` |
| `wifi_setup_device.cpp:72-76` | Fields defined once in `core::portal_params` and rendered here, so the two portals cannot drift | `portal.md#one-field-table` | `// See docs/modules/portal.md#one-field-table` |
| `wifi_setup_device.cpp:81-85` | `WiFiManagerParameter` keeps `custom` as a pointer; each field needs its own buffer outliving the portal; a refresh must rewrite in place, not reassign | `portal.md#attribute-buffer-ownership` | `// Rewrite in place, never reassign: WiFiManagerParameter retains these.`<br>`// See docs/modules/portal.md#attribute-buffer-ownership` — **KEEP INLINE, §7** |

### 5.20 Native platform (`src/platform/native/`)

| File:lines | Substance | Destination | Pointer to leave |
|---|---|---|---|
| `button_sdl.cpp:1-14` | SDL keycode mapped onto an emulated pin, same active-LOW polarity as GPIO 9, so the harness exercises real tap-vs-hold timing; no interrupt, so edges are recovered by polling; latch semantics identical | `native-harness.md#boot-button-on-the-keyboard` | `// See docs/modules/native-harness.md#boot-button-on-the-keyboard` |
| `button_sdl.cpp:25-29` | Mirrors `config::kBootPin` (GPIO 9); `pins.h` is device-only so it cannot be included here | `constraints.md#layering` | `// Mirrors config::kBootPin — pins.h is device-only. docs/constraints.md#layering` |
| `button_sdl.cpp:44-46` | Idle must be HIGH, matching `INPUT_PULLUP`; also covers being called before the panel is up | **stay** | KEEP INLINE |
| `button_sdl.cpp:53` | Active LOW | **stay** | KEEP INLINE |
| `button_sdl.cpp:66-71` | SDL pumps events on the main thread while this runs on the user thread, so the emulated GPIO byte crosses threads; sample once into a local — racy but benign (single byte, no torn value), worst case an edge noticed one 10 ms iteration late; locking would buy precision SDL timing does not have | `native-harness.md#boot-button-on-the-keyboard` | `// Deliberately unlocked cross-thread byte read — racy but benign.`<br>`// See docs/modules/native-harness.md#boot-button-on-the-keyboard` — **KEEP INLINE, §7** |
| `button_sdl.cpp:82` | Latched so the reset fires once per press | **stay** | KEEP INLINE |
| `button_sdl.cpp:91-93` | Release edge; the device's ISR classifies here too; shorter than `kBootTapMinMs` is bounce | **stay** | KEEP INLINE |
| `display_native.cpp:1,15` | File purpose; window magnification, logical panel stays 240×240 so layout is identical | **stay** | KEEP INLINE |
| `display_native.cpp:23-28` | **Title must be set before `init()`**: with no window yet `Panel_sdl` only records it and `SDL_CreateWindow` applies it on the main thread; afterwards it calls `SDL_SetWindowTitle` from a worker, which SDL3/sdl2-compat rejects; reach the panel through `getPanel()` | `constraints.md#native-main-thread` | `// Before init(): SDL window calls from this thread abort the harness.`<br>`// See docs/constraints.md#native-main-thread` — **KEEP INLINE, §7** |
| `display_native.cpp:32` | No backlight to dim on an SDL window | **stay** | KEEP INLINE |
| `display_native.cpp:36-38` | Hard failure natively: falling back to bitmap GFX fonts silently changes every text metric the layout is derived from, so the harness would look fine while misreporting exactly what it exists to measure | `native-harness.md#fidelity` | `// Hard failure: a font fallback would silently invalidate every metric.`<br>`// See docs/modules/native-harness.md#fidelity` |
| `font_blob_file.cpp:1-11` | Function-local static, never resized; `loadFont()` stores the pointer and reads lazily, so a local or reallocated buffer reads freed memory — and would usually appear to work | `constraints.md#font-blob-lifetime` | `// Never resize or localise this buffer: loadFont() retains the pointer.`<br>`// See docs/constraints.md#font-blob-lifetime` — **KEEP INLINE, §7** |
| `http_curl.cpp:1-18` | Three deliberate differences: timeout capped well below what the caller asks (a stall freezes the single-threaded harness); `PollFn` in curl's progress callback; **TLS verification left ON — do NOT "align" with the device** | `native-harness.md#curl-backend`, `#timeout-cap`, `#tls-verification-stays-on` | `// See docs/modules/native-harness.md#curl-backend` |
| `http_curl.cpp:15-17` (TLS clause) | Device does `setInsecure()` because it has no CA bundle and no wall clock; the host has both | `native-harness.md#tls-verification-stays-on` | `// TLS verification stays ON here. Do NOT "align" this with the device.`<br>`// See docs/modules/native-harness.md#tls-verification-stays-on` — **KEEP INLINE, §7** |
| `http_curl.cpp:31-34` | Upper bound regardless of caller; this blocks `loop()`, so a long stall is a frozen UI | `native-harness.md#timeout-cap` | `// See docs/modules/native-harness.md#timeout-cap` |
| `http_curl.cpp:39,42` | `curl_global_init` is not reentrant; nothing useful on failure | **stay** | KEEP INLINE |
| `http_curl.cpp:51-55` | Runs on progress ticks — the only place the harness gets control back mid-request; non-zero would abort the transfer | **stay** | KEEP INLINE |
| `http_curl.cpp:64` | Closes the handle on every exit path | **stay** | KEEP INLINE |
| `http_curl.cpp:87-89` | Host collects and replays; the device streams instead, where holding tens of KB runs the heap out; here it is free | `constraints.md#no-whole-body-buffering` | `// Host buffers; the device must not. See docs/constraints.md#no-whole-body-buffering` |
| `http_curl.cpp:111-112,114-115,118` | Empty accept-encoding = every encoding this curl can decode; NOSIGNAL avoids a SIGALRM handler landing in the SDL event loop; NOPROGRESS must be cleared | **stay** | KEEP INLINE |
| `http_curl.cpp:125-126` | A partial body is worse than none | **stay** | KEEP INLINE |
| `kv_json_file.cpp:1-19` | JSON file mirroring NVS namespace-for-namespace; whole-file rewrite per put mirrors NVS committing on `end()`; temp file + `rename()` so an interrupted write cannot truncate; **pretty-printed on purpose** because hand-editing to jump the harness to another airport is a first-class workflow, which is also why a malformed file only warns | `native-harness.md#settings-json` | `// See docs/modules/native-harness.md#settings-json` |
| `kv_json_file.cpp:43,62,66,74,78,91,107-108,113,145,152,159-160,167` | Resolved once; 0700 because this may hold a home location; absence is not an error; top level is the namespace map; the v7 `containsKey()` spelling; namespace created on demand | `native-harness.md#settings-json` | Migrate with the file header |
| `kv_json_file.cpp:135,183-184,194` | `fsync` before rename or it is not atomic; `is<uint8_t>()` is range-checked so a hand-edited 300 falls back rather than wrapping; `is<double>()` accepts integers so `"lat": 52` works | `native-harness.md#settings-json` | **KEEP INLINE** (1 line each) — they justify code that looks removable |
| `main_native.cpp:1-12` | Runs the real shared `setup()`/`loop()` under SDL; `Panel_sdl::main()` owns the true main thread and calls `user_func` on a worker — the same shape Arduino gives on device, so `main.cpp` needs no native special-casing; `-DPLANE_RADAR_FRAME_HASH` dumps font metrics + frame hash | `native-harness.md#why-not-an-emulator`, `#frame-hash` | `// See docs/modules/native-harness.md#why-not-an-emulator` |
| `main_native.cpp:24` | Defined in `src/main.cpp`, shared verbatim | **stay** | KEEP INLINE |
| `main_native.cpp:34` | Mirrors `radar_display.cpp`'s `findVlwSizeForHeight()` binary search **exactly** | `native-harness.md#fidelity` | `// Must mirror radar_display.cpp's findVlwSizeForHeight() exactly.`<br>`// See docs/modules/native-harness.md#fidelity` — **KEEP INLINE, §7** |
| `main_native.cpp:86` | `#endif // PLANE_RADAR_FRAME_HASH` | **stay** | KEEP INLINE |
| `platform_native.cpp:1` | File purpose | **stay** | KEEP INLINE |
| `platform_native.cpp:23-24` | stdout line-buffered so output interleaves sensibly with SDL's logging | **stay** | KEEP INLINE |
| `platform_native.cpp:47-48` | Real process exit, not in-process restart | `constraints.md#reboot-is-process-exit` | `// See docs/constraints.md#reboot-is-process-exit` |
| `portal_server.h:3-8` | Private seam; nothing outside `src/platform/native/` may depend on it; the device has no counterpart | `constraints.md#layering` | `// Private to src/platform/native/. See docs/constraints.md#layering` |
| `portal_server.h:13-19` | Two files write the simulated credentials; namespace matches the device's Wi-Fi prefs namespace | `portal.md#force-portal-flag` | `// See docs/modules/portal.md#force-portal-flag` |
| `portal_server.h:23-28` | Idempotent; false on bind failure (typically a second harness); the caller carries on without a portal rather than dying | **stay** | KEEP INLINE |
| `portal_server.h:31-37` | Never blocks; safe when never started; idempotent close | **stay** | KEEP INLINE |
| `portal_server.h:40-46` | Latched rather than polled from storage so a resubmission of the same SSID still ends the boot-time wait | `portal.md#ssid-is-not-in-the-table` | KEEP the contract; add `// Latched deliberately — docs/modules/portal.md#ssid-is-not-in-the-table` |
| `portal_server.cpp:1-22` | Loopback only (hand-rolled parser has no business on a café LAN); single-threaded and non-blocking, pumped from `wifiLoop()`, bounded by poll budgets, one response per connection so a parked tab cannot starve the pump; the form carries one field the table does not (SSID) | `portal.md#loopback-only`, `#single-threaded-pump`, `#ssid-is-not-in-the-table` | `// See docs/modules/portal.md#loopback-only and #single-threaded-pump` |
| `portal_server.cpp:49,52,55-58,63` | Request ceiling; per-pump work budget; per-connection I/O budget and why the normal path spends no time there; buffer purpose | **stay** | KEEP INLINE |
| `portal_server.cpp:70,122,259,353,453` | Banners | — | — (delete) |
| `portal_server.cpp:77-80` | macOS has no `MSG_NOSIGNAL`, so a closed tab mid-response would take the harness down with SIGPIPE; disabled per socket | `portal.md#single-threaded-pump` | `// No MSG_NOSIGNAL on macOS: SIGPIPE would kill the harness.` — **KEEP INLINE, §7** |
| `portal_server.cpp:82,212,219,230,245,248,275,420,455,547` | Bounded best-effort write; caller drops oversize; completeness test; data flowing so do not spend a poll; peer closed early; label placement; not in the table; serves one request; EAGAIN in the common case | **stay** | KEEP INLINE |
| `portal_server.cpp:137-143` | Truncated escape at the end must not read past the buffer; hex digits bounds-checked before touching; a non-hex escape passes through as a literal `%` rather than being guessed | `portal.md#form-urlencoded-decode` | `// See docs/modules/portal.md#form-urlencoded-decode` — **KEEP INLINE, §7** |
| `portal_server.cpp:327-328` | No PSK to check, so the password is accepted and discarded; exists so the form matches the device's shape | `portal.md#ssid-is-not-in-the-table` | `// See docs/modules/portal.md#ssid-is-not-in-the-table` |
| `portal_server.cpp:397-407` | **The absent-checkbox pass is load-bearing**: an unchecked HTML checkbox is not submitted at all, so a form carrying only ticked boxes makes an untick indistinguishable from "field not in this form" and the user's change silently vanishes; every unmentioned checkbox is therefore applied as `""` first; the device gets this free from WiFiManager | `portal.md#absent-checkbox-pass` | `// Load-bearing: an unchecked box is not submitted at all, so unticks would`<br>`// silently vanish. See docs/modules/portal.md#absent-checkbox-pass` — **KEEP INLINE, §7** |
| `portal_server.cpp:427-428` | Exactly once per submission; lat/lon staged during apply and only persistent here, as a validated pair | `portal.md#staged-latlon-commit` | `// See docs/modules/portal.md#staged-latlon-commit` |
| `portal_server.cpp:480-481` | Whitelist not blacklist: this parser is only trustworthy on the two shapes it was written for | `portal.md#single-threaded-pump` | `// Whitelist, not blacklist. See docs/modules/portal.md#single-threaded-pump` |
| `portal_server.cpp:519` | Loopback only, never `INADDR_ANY` | `portal.md#loopback-only` | `// Loopback only, never INADDR_ANY. See docs/modules/portal.md#loopback-only` — **KEEP INLINE, §7** |
| `portal_server.cpp:552-553` | One response per connection; no keep-alive, so a parked tab cannot occupy the pump | `portal.md#single-threaded-pump` | `// See docs/modules/portal.md#single-threaded-pump` |
| `wifi_setup_native.cpp:1-24` | No Wi-Fi here; what the file exists for is the *shape* of the boot flow and the real shared connecting animation; BOOT entry points live in `button_sdl.cpp`; the four documented native/device divergences | `native-harness.md#simulated-radio` | `// See docs/modules/native-harness.md#simulated-radio` |
| `wifi_setup_native.cpp:41,44-48` | Mirrors the device's force-portal flag (same namespace and key); why 2 s of simulated association | `portal.md#force-portal-flag` | `// See docs/modules/portal.md#force-portal-flag` |
| `wifi_setup_native.cpp:54-61` | `wifiLoop()` is called from `loop()` **and** handed to `HttpClient::get()` as its poll hook, so it can re-enter with a previous call still on the stack; serving a portal request from within one would re-enter the accept loop with a half-served connection, so the nested call is dropped | `native-harness.md#reentrancy-guard` | `// Reentrancy guard: wifiLoop() is also the HTTP poll hook.`<br>`// See docs/modules/native-harness.md#reentrancy-guard` — **KEEP INLINE, §7** |
| `wifi_setup_native.cpp:90-94` | The point of the whole file: the connecting UI is real shared code at the real frame interval; only the outcome is faked | `native-harness.md#simulated-radio` | `// See docs/modules/native-harness.md#simulated-radio` |
| `wifi_setup_native.cpp:99-100` | Same ordering as `waitForLinkWithUi()`: the button stays live while the spinner runs, which is how a stuck boot is escaped | **stay** | KEEP INLINE |
| `wifi_setup_native.cpp:108-112` | Blocks until an SSID is submitted, mirroring `openConfigPortal()`; blocking during `setup()` is the device's behaviour too | **stay** | KEEP INLINE |
| `wifi_setup_native.cpp:148-149` | `exit(0)`, not a restart: re-entering `setup()` would carry guard statics over | `constraints.md#reboot-is-process-exit` | `// See docs/constraints.md#reboot-is-process-exit` |
| `wifi_setup_native.cpp:192` | See `s_in_wifi_loop` for why the nested call is dropped | `native-harness.md#reentrancy-guard` | `// Nested call dropped. docs/modules/native-harness.md#reentrancy-guard` — **KEEP INLINE, §7** |
| `wifi_setup_native.cpp:195-198` | Device order: button polled before the portal is serviced so a long hold wins over a slow request; `bootButtonPollLongPress()` may not return, which is why nothing below it is required for correctness | `native-harness.md#simulated-radio` | `// Device order; this call may not return. docs/modules/native-harness.md#simulated-radio` — **KEEP INLINE, §7** |

### 5.21 Remaining `src/` files

| File | Action |
|---|---|
| `src/core/airport_find.cpp` (2), `src/core/geo.cpp` (1), `src/core/portal_params.cpp` (2), `src/core/tap_gesture.cpp` (2), `src/ui/display_font.cpp` (1) | Namespace closers only — keep all |
| `src/ui/runway_overlay.cpp:182-184` — stretch about the midpoint so the strip reads while centre and bearing stay correct; done **in degrees before projection** so exaggeration scales with the preset | → `render.md#runway-exaggeration`; pointer `// See docs/modules/render.md#runway-exaggeration` |
| `src/ui/status_screens.cpp:109` — truncate with `…` above a width | keep (API CONTRACT) |
| `src/ui/status_screens.cpp:210-213` — instructions come from the platform; the device returns exactly the strings that used to be inline, so the rendered screen is unchanged | → `ARCHITECTURE.md#portability-seam`; pointer `// See ARCHITECTURE.md#portability-seam` |

## 6. Quantification

**RE**=restates, **RA**=rationale, **AC**=api contract, **TD**=todo, **SM**=section marker (banners + namespace closers). "After" = projected comment lines once the strip is done, counting pointers, retained API contracts, retained namespace closers, and the §7 inline exceptions.

### `include/`

| File | Lines | Cmt | % | RE | RA | AC | TD | SM | After |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `config.h` | 104 | 58 | 55% | 0 | 34 | 9 | 8 | 7 | 19 |
| `core/adsb.h` | 39 | 15 | 38% | 0 | 10 | 4 | 0 | 1 | 7 |
| `core/aircraft.h` | 27 | 10 | 37% | 0 | 6 | 3 | 0 | 1 | 5 |
| `core/airport_find.h` | 10 | 2 | 20% | 0 | 0 | 1 | 0 | 1 | 2 |
| `core/geo.h` | 76 | 36 | 47% | 0 | 9 | 26 | 0 | 1 | 29 |
| `core/large_airports.h` | 30 | 2 | 6% | 0 | 1 | 0 | 0 | 1 | 2 |
| `core/platform.h` | 177 | 83 | 46% | 0 | 59 | 15 | 0 | 9 | 23 |
| `core/portal_params.h` | 71 | 42 | 59% | 0 | 23 | 18 | 0 | 1 | 22 |
| `core/settings.h` | 146 | 65 | 44% | 0 | 44 | 14 | 0 | 7 | 20 |
| `core/tap_gesture.h` | 20 | 4 | 20% | 0 | 0 | 3 | 0 | 1 | 4 |
| `core/terrain.h` | 149 | 78 | 52% | 0 | 34 | 42 | 0 | 2 | 48 |
| `platform/device/lgfx_config_device.hpp` | 37 | 1 | 2% | 0 | 0 | 1 | 0 | 0 | 1 |
| `platform/device/pins.h` | 33 | 15 | 45% | 0 | 10 | 2 | 0 | 3 | 6 |
| `platform/native/lgfx_config_native.hpp` | 19 | 14 | 73% | 0 | 14 | 0 | 0 | 0 | 2 |
| `platform/png_decode.h` | 66 | 47 | 71% | 0 | 32 | 14 | 0 | 1 | 20 |
| `platform/wifi_setup.h` | 28 | 15 | 53% | 0 | 8 | 7 | 0 | 0 | 9 |
| `ui/display.h` | 20 | 8 | 40% | 0 | 8 | 0 | 0 | 0 | 2 |
| `ui/display_font.h` | 15 | 3 | 20% | 0 | 0 | 3 | 0 | 0 | 3 |
| `ui/radar_display.h` | 28 | 15 | 53% | 0 | 12 | 2 | 0 | 1 | 5 |
| `ui/radar_range.h` | 81 | 24 | 29% | 0 | 14 | 9 | 0 | 1 | 13 |
| `ui/radar_theme.h` | 149 | 51 | 34% | 0 | 30 | 20 | 0 | 1 | 24 |
| `ui/runway_overlay.h` | 9 | 1 | 11% | 0 | 0 | 0 | 0 | 1 | 1 |
| `ui/status_screens.h` | 9 | 1 | 11% | 0 | 0 | 1 | 0 | 0 | 1 |
| `ui/terrain_overlay.h` | 16 | 8 | 50% | 0 | 0 | 7 | 0 | 1 | 8 |
| **include/ total** | **1,359** | **598** | **44%** | **0** | **348** | **201** | **8** | **41** | **276** |

### `src/` (excluding generated `large_airports_data.cpp`)

| File | Lines | Cmt | % | RE | RA | AC | TD | SM | After |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `core/adsb.cpp` | 297 | 29 | 9% | 0 | 24 | 3 | 0 | 2 | 9 |
| `core/airport_find.cpp` | 63 | 2 | 3% | 0 | 0 | 0 | 0 | 2 | 2 |
| `core/geo.cpp` | 86 | 1 | 1% | 0 | 0 | 0 | 0 | 1 | 1 |
| `core/portal_params.cpp` | 161 | 2 | 1% | 0 | 0 | 0 | 0 | 2 | 2 |
| `core/settings.cpp` | 384 | 16 | 4% | 2 | 6 | 0 | 0 | 8 | 4 |
| `core/terrain.cpp` | 499 | 106 | 21% | 3 | 97 | 4 | 0 | 2 | 30 |
| `main.cpp` | 195 | 19 | 9% | 0 | 18 | 0 | 0 | 1 | 5 |
| `platform/device/display_device.cpp` | 15 | 1 | 6% | 0 | 0 | 1 | 0 | 0 | 1 |
| `platform/device/font_blob_embedded.cpp` | 26 | 7 | 26% | 0 | 6 | 0 | 0 | 1 | 2 |
| `platform/device/http_arduino.cpp` | 170 | 23 | 13% | 0 | 18 | 3 | 0 | 2 | 6 |
| `platform/device/kv_nvs.cpp` | 105 | 11 | 10% | 0 | 8 | 1 | 0 | 2 | 4 |
| `platform/device/platform_device.cpp` | 49 | 3 | 6% | 0 | 1 | 1 | 0 | 1 | 3 |
| `platform/device/wifi_setup_device.cpp` | 501 | 12 | 2% | 0 | 10 | 0 | 0 | 2 | 5 |
| `platform/native/button_sdl.cpp` | 101 | 34 | 33% | 0 | 26 | 7 | 0 | 1 | 8 |
| `platform/native/display_native.cpp` | 45 | 13 | 28% | 0 | 10 | 2 | 0 | 1 | 6 |
| `platform/native/font_blob_file.cpp` | 56 | 13 | 23% | 0 | 11 | 0 | 0 | 2 | 3 |
| `platform/native/http_curl.cpp` | 142 | 42 | 29% | 0 | 30 | 10 | 0 | 2 | 11 |
| `platform/native/kv_json_file.cpp` | 215 | 42 | 19% | 0 | 32 | 8 | 0 | 2 | 8 |
| `platform/native/main_native.cpp` | 101 | 16 | 15% | 0 | 14 | 0 | 0 | 2 | 5 |
| `platform/native/platform_native.cpp` | 58 | 7 | 12% | 0 | 4 | 1 | 0 | 2 | 5 |
| `platform/native/portal_server.h` | 47 | 32 | 68% | 0 | 26 | 5 | 0 | 1 | 10 |
| `platform/native/portal_server.cpp` | 575 | 78 | 13% | 0 | 55 | 16 | 0 | 7 | 17 |
| `platform/native/wifi_setup_native.cpp` | 202 | 58 | 28% | 0 | 48 | 8 | 0 | 2 | 11 |
| `platform/png_decode.cpp` | 732 | 94 | 12% | 0 | 65 | 22 | 0 | 7 | 32 |
| `ui/display_font.cpp` | 49 | 1 | 2% | 0 | 0 | 0 | 0 | 1 | 1 |
| `ui/radar_display.cpp` | 696 | 16 | 2% | 2 | 11 | 1 | 0 | 2 | 8 |
| `ui/runway_overlay.cpp` | 307 | 5 | 1% | 0 | 3 | 0 | 0 | 2 | 3 |
| `ui/status_screens.cpp` | 246 | 6 | 2% | 0 | 3 | 1 | 0 | 2 | 4 |
| `ui/terrain_overlay.cpp` | 139 | 37 | 26% | 0 | 34 | 1 | 0 | 2 | 11 |
| **src/ total** | **6,311** | **728** | **12%** | **7** | **570** | **95** | **0** | **60** | **224** |

### Repo-wide

| | Lines | Comment lines | % |
|---|---:|---:|---:|
| **Before** | 7,670 | **1,326** | 17% |
| **After** | ~7,670 | **~500** | **~7%** |
| Removed | — | ~826 | |

Composition of the ~500 remaining: **~295 API CONTRACT** in headers, **101 namespace closers**, **~75 one-line pointers**, **~25 §7 inline exceptions**, **≤8 TODO**.

Where the removed 826 go: ~815 RATIONALE lines become roughly **11,000 words** across `docs/constraints.md`, `ARCHITECTURE.md` and five module docs. **Nothing is lost.** Only ~177 lines (RESTATES + banners) are genuinely deleted.

## 7. Danger cases — where I recommend overruling you

Twenty-five sites where moving the comment out materially raises the risk of a future editor breaking something. Common shape: **the code looks removable, simplifiable or wrong, and doing the obvious thing produces a failure that is silent, delayed, or looks like unrelated hardware trouble.** A docs pointer does not help, because the editor has to already suspect there is something to look up.

My recommendation is the same everywhere and it is cheap: **one or two lines inline, ending in the docs anchor.** 25 sites × ~1.5 lines ≈ 38 lines across 7,670 — 0.5% of the tree. The full explanation still lives in docs; the inline line exists only to interrupt the editor.

### Tier 1 — recommend inline, would strongly resist removing

| # | Site | If the comment moves out | Failure mode |
|---|---|---|---|
| 1 | `terrain.cpp:29-37` — one cached grid | "Cache all four presets, it's only 3 KB each" is a *natural, tempting, obviously-correct-looking* refactor | ADS-B fetch intermittently cannot open a TLS socket. Aircraft silently stop appearing. Nothing points at `.bss`. |
| 2 | `terrain_overlay.cpp:22-32` — `kFracBits` | Someone "cleans up" the shifts into float lerps for readability. It works. | Frame time 297 → ~470 ms; the UI visibly stutters. Passes every test. Nobody profiles a UI change. |
| 3 | `radar_display.h:14-25` + new comment at `radar_display.cpp:684` | The INVARIANT ("no frame composed while scratch is on loan") is enforced by **nothing** | A frame composed mid-decode corrupts the tile *and* shows garbage. Intermittent, unreproducible. |
| 4 | `main.cpp:114-118` — repaint on the download-stop edge | Looks like an over-complicated `if`; "simplify to repaint whenever `ready`" or "repaint every loop" | Either terrain never appears, or a full redraw every 10 ms. |
| 5 | `platform.h:55-61` + `font_blob_file.cpp:1-11` — font blob lifetime | A local buffer, a `std::vector`, or an early `free()` all compile and **usually appear to work** | Reads freed memory. Font metrics drift, so the harness silently misreports the one thing it exists to measure. |
| 6 | `http_curl.cpp:15-17` — TLS verification stays ON | This comment is a direct anti-instruction to a future agent asked to "make native match device" | A dev tool that accepts any certificate, with `~/.plane-radar/settings.json` holding a plaintext SSID. |
| 7 | `portal_server.cpp:397-407` — absent-checkbox pass | A loop applying `""` to fields the request did not mention reads exactly like dead code | Unticking a checkbox in the portal silently does nothing. User-visible, hard to attribute. |
| 8 | `settings.cpp:19-23` + `settings.h:57-62` — frozen keys | "Rename `useKm` to `use_km` for consistency" is the kind of thing a linter suggests | Every device in the field silently reverts that setting on update. Unrecoverable without a migration. |
| 9 | `platform.h:114-122` + `adsb.cpp:197-205` — never buffer a body | "Just read it into a string, it's simpler" is the single most likely regression in this codebase | `std::bad_alloc` → firmware abort, but **only at the wider range presets**, so it passes casual testing. |
| 10 | `png_decode.h:35-43` + `png_decode.cpp:40-44` — scratch layout | The `static_assert` catches a too-big `Work`, but nothing catches "raise `kScratchBytes` to make the assert pass" | Scratch request exceeds the sprite, lender returns `nullptr`, terrain silently never loads. |

### Tier 2 — recommend inline

| # | Site | Reason |
|---|---|---|
| 11 | `png_decode.cpp:552-556` — refuse past the last row **in `emit()`** | Moving the check to `run()` is the obvious tidy-up and breaks `PixelFn`'s bounds guarantee that `onPixel()` relies on. |
| 12 | `png_decode.cpp:426-427` — code-length table aliases the literal table | Looks like a bug. Someone will "fix" it by adding a third `Huffman`, blowing the scratch budget. |
| 13 | `png_decode.cpp:473-474` + `616-617` — drop only partial bits | `bit_count_ = 0; bits_ = 0;` looks equivalent and corrupts every stored block and every Adler check. |
| 14 | `png_decode.cpp:608-614` — Adler-32 is the only end-to-end check | Verification looks like optional cost to cut on a slow target. Removing it makes corrupt tiles into plausible terrain. |
| 15 | `terrain.cpp:295-299` — longitude deliberately NOT clamped | Asymmetry looks like an oversight. "Fixing" it breaks every antimeridian-crossing view. |
| 16 | `terrain.cpp:465-468` — discard partial grids | `return false` after successfully decoding tiles looks wasteful. Publishing the gaps paints a false shoreline. |
| 17 | `terrain.cpp:218-222` — "those borrowed pixels were the frame" | The one-line function body gives no hint that a repaint is owed. |
| 18 | `wifi_setup_native.cpp:54-61` + `192` — reentrancy guard | An early-return guard on a bool looks like dead code. Removing it re-enters the accept loop on a half-served connection. |
| 19 | `display_native.cpp:23-28` — set the title before `init()` | Moving it after `init()` is more natural ordering and **aborts the harness**. |
| 20 | `pins.h:23-24` — GPIO2 strapping pin | Pin reassignment is routine; this one bricks boot and gives no clue why. |
| 21 | `ui/display.h:3-10` — the single sanctioned `#ifdef` | Without the statement, a second `#ifdef` in shared code is the path of least resistance, and the two-destination discipline erodes one `#ifdef` at a time. |
| 22 | `radar_range.h:22-27` — `static` is load-bearing | Self-enforcing (link error), so lower risk — but it also stops a "modernise to `inline`" change that breaks pre-C++17 parts of the Arduino build. One line. |
| 23 | `wifi_setup.h:3-10` — `wifiIsConnected()` ≠ `wifiLinkUp()` | Two nearly identical predicates; "deduplicate" changes the WiFi-lost/grace/reconnect timing in `loop()`. |
| 24 | `main_native.cpp:34` — mirrors `findVlwSizeForHeight()` exactly | Duplicated code with no marker gets "refactored" or drifts, and the fidelity baseline quietly stops meaning anything. |
| 25 | `portal_server.cpp:77-80` (SIGPIPE), `137-143` (truncated escape), `519` (loopback); `button_sdl.cpp:66-71` (deliberate unlocked read); `kv_json_file.cpp:135,183-184,194` | Small hardening details that each look like removable defensiveness; each is one line. |

### One I am happy to lose

`settings.h:3-14`'s merge history ("merged from the former `services/radar_location.cpp` and `ui/radar_range.cpp`") and `geo.h:3-10`'s extraction history are **archaeology, not constraints**. Git has this. Drop it from the docs too — it is the same stale provenance that made `.cursor/rules/` wrong (§9).

### The counter-argument, stated fairly

You may reasonably reply: "if I let 25 exceptions in, the next reviewer lets 25 more and I am back where I started." That is real, and the mitigation is a rule rather than a judgement call:

> An inline comment is permitted only when **(a)** it is at most two lines, **(b)** its last clause is a `docs/` anchor, and **(c)** it names a *failure*, not a *mechanism*. "Fixed point because no FPU" is permitted; "interpolation weights are fixed point" is not.

Under that rule the 25 above qualify and almost nothing else in this tree does. I would take this rule over a hard zero.

## 8. TODO / FIXME / HACK inventory

`grep -nE 'TODO|FIXME|HACK|XXX|WORKAROUND'` over `src/`, `include/`, `test/`, `Makefile`, `platformio.ini`: **exactly one hit.** For a codebase this size that is unusual and good.

| # | Location | Text | Assessment |
|---|---|---|---|
| 1 | `include/config.h:43-50` | `TODO(step 1)`: `kDisplayRgbOrder` pending the on-hardware colour check. Read by **both** the LGFX device config and `ui/radar_display.cpp`'s `initPalette()`, which software-swaps R/B **for the aircraft colour only**. Once the panel's real colour order is confirmed the swap is deleted and the constant moves to `pins.h`. | **Real, latent, and worth resolving before the strip.** Not a latent crash — a latent *inconsistency*. One constant drives two mechanisms that could disagree: the LGFX panel-level order and a hand-rolled per-colour swap. If they ever disagree, exactly one colour on screen is wrong and everything else is right, which is the hardest class of visual bug to attribute. The swap being applied to the aircraft colour **only** is itself the smell: if the panel order were genuinely wrong, every colour would need it. **Action:** put the board on a desk, check whether red renders red with the swap removed, then either delete the swap or apply it uniformly, and move `kDisplayRgbOrder` to `pins.h` as the TODO says. Closes in minutes and removes 8 comment lines *and* a real hazard. |

Two related items **not** marked TODO but found during the audit:

| Location | Problem | Severity |
|---|---|---|
| `src/core/settings.cpp:30` | `constexpr uint8_t kDefaultRangeIndex = 1;  // 40 NM ring` — **index 1 is the 20 NM preset.** `kRangePresets` is `{10, 20, 40, 80}` NM and `settings.h:29` correctly documents "20 NM — default". | **Comment is factually wrong.** Harmless today (the value is right, the comment lies) but this is exactly how someone "fixes" `kDefaultRangeIndex` to `2`. Delete the comment; do not migrate it. Confirms comment rot here is real and that the strip is worth doing. |
| `include/platform/png_decode.h:9` vs `README.md:122` vs `kScratchBytes` | Decoder size given as **~44 KB**, **~36 KB** and **35,840 B** in three places. | **Low, but it is drift in the single most important number in the repo.** `constraints.md` states 35 KB *with the derivation* so the value can be re-checked instead of remembered. Fix `README.md:122` in the same pass. |

## 9. Audit of `.cursor/` and `README.md`

The user is right that `.cursor/rules/plane-radar-project.mdc` is stale, and it is worse than reported: `alwaysApply: true` means **every Cursor request in this repo is prefixed with a description of a namespace layout that does not exist.** That is not neutral — an agent told `services::adsb` exists will write `services::adsb`, fail to compile, then "fix" it by guessing. The highest-value single change in this whole document is deleting or rewriting that file.

Severity: **CRITICAL** = actively misdirects an agent into wrong code; **HIGH** = names a nonexistent file/symbol; **MED** = wrong values; **LOW** = incomplete/cosmetic.

### `.cursor/rules/plane-radar-project.mdc` — `alwaysApply: true`

| Claim | Reality | Sev |
|---|---|---|
| Namespaces `services::adsb`, `services::location` | **Do not exist.** Real: `core::adsb`, `core::settings` | **CRITICAL** |
| Namespaces list omits `core::terrain`, `core::settings`, `core::gesture`, `core::geo`, `core::platform`, `core::airport`, `core::portal`, `ui::terrain`, `platform_png` | All exist | **CRITICAL** |
| Layer map: "Hardware → `ui/`, `src/platform/`"; "Services → `include/platform/`, `src/platform/`"; "Data → `include/core/`, `src/core/`" | Wrong on three of six rows. Real: `core/` is portable logic (no Arduino/LovyanGFX), `ui/` is LovyanGFX drawing, `platform/` is the per-destination seam | **CRITICAL** |
| No mention of the native destination at all | Half the `src/` tree | **CRITICAL** |
| No mention of the memory constraint or no-FPU | The two things an agent most needs before editing | **CRITICAL** |
| Display stack "LovyanGFX via `lgfx_config.hpp`" | File does not exist. Real: `include/platform/device/lgfx_config_device.hpp`, `include/platform/native/lgfx_config_native.hpp` | HIGH |
| "PlatformIO env: `supermini` only" | Seven envs: `supermini`, `supermini_debug`, `native`, `native_test`, `native_test_fetch`, `native_test_png`, `native_test_live` | HIGH |
| Range presets live in `ui/radar_range.h` | Live in `include/core/settings.h`; `radar_range.h` is a forwarder | HIGH |
| `data::large_airports` listed as the only data namespace under "Data → `include/core/`" | The namespace exists; the surrounding layer description is wrong | LOW |
| "No OTA slot — single ~3 MB app partition"; "Do not hand-edit generated `large_airports_data.cpp`" | Correct | ✅ |

### `.cursor/rules/esp32-conventions.mdc`

| Claim | Reality | Sev |
|---|---|---|
| `planeradar` keys `rangeIdx`, `useMiles`, `showRwys` | Real: `rangeIdx`, **`useKm`**, `showRwys`, **`showTerr`**, **`sites`**, **`siteIdx`**. `useMiles` is the **retired** key whose reuse would invert the setting — the rule states the exact trap the code warns against | **CRITICAL** |
| `services::adsb::setPollFn(wifiLoop)` | `core::adsb::setPollFn` | **CRITICAL** |
| Credential reset clears location via `services::location::clear()` | `core::settings::clearLocation()` | **CRITICAL** |
| NVS table file column: `wifi_setup.cpp` | `src/platform/device/wifi_setup_device.cpp` | HIGH |
| NVS table file column: `radar_location.cpp` | **Does not exist.** `src/core/settings.cpp` | HIGH |
| NVS table file column: `radar_range.cpp` | **Does not exist.** `src/core/settings.cpp` | HIGH |
| "Pins, SPI, WiFi timing, ADS-B interval → `include/config.h`" | Pins and SPI moved to `include/platform/device/pins.h`; config.h must stay portable | HIGH |
| Reset path does not mention `showTerr` | `unitsReset()` resets it | MED |
| "Do not merge namespaces or hold overlapping `Preferences` handles"; `WiFi.setTxPower(WIFI_POWER_8_5dBm)` in both AP and STA; BOOT GPIO 9 active LOW with `bootButtonPollLongPress()`/`bootButtonConsumeTap()` | Correct | ✅ |

### `.cursor/rules/radar-ui.mdc`

| Claim | Reality | Sev |
|---|---|---|
| Presets "5, 10, 15, 25 km (default index 1 = 10 km)" | `{10, 20, 40, 80} NM` ⇒ ring-3 `{18.5, 37.0, 74.1, 148.2} km`; default index 1 = **20 NM / 37 km** | **CRITICAL** |
| `radarDisplayRefreshAircraft()` "redraw aircraft on cached sprite — **no full-screen clear**" | **False.** It calls `renderFrame()` → `drawStaticGrid()` → `gfx.fillScreen()`. Both paths do a full composite. An agent trusting this will not understand why terrain repaints work | **CRITICAL** |
| No mention of no-FPU / fixed-point rule in a **rendering** rule | The rule most needed here | **CRITICAL** |
| Presets belong in `ui/radar_range.h` | `include/core/settings.h` | HIGH |
| No mention of `ui::terrain` / `terrain_overlay.cpp` in a UI-scoped rule | Terrain is now the largest per-frame cost | HIGH |
| `radarDisplayDraw()` "First paint, range change" | Also site change, terrain-download edge, WiFi reconnect | MED |
| Tag placement toward centre; sprite frame buffer `LGFX_Sprite s_frame`; constants in `radar_theme.h`; palette from byte targets; `outer_km = ring3_km × 4/3` | Correct | ✅ |

### `.cursor/rules/platformio-ci.mdc`

| Claim | Reality | Sev |
|---|---|---|
| No mention of `upload_flags = --no-stub` / `upload_speed = 115200` and why | The single most operationally important fact in this file's scope, and `platformio.ini` itself carries no comment on it | **CRITICAL** |
| No mention that the serial monitor must be closed before upload | Undocumented everywhere | **CRITICAL** |
| "Env: `supermini`" (singular) | Seven envs | HIGH |
| No mention of `native_test_png`'s SDL constraint | Documented only in `platformio.ini:175-185` | HIGH |
| Partitions "(4 MB, no OTA)"; `-DWM_MDNS`; `board_build.embed_files`; `-std=gnu++17`; merged image at 0x0; download mode; `build_large_airports.py` → two generated files; workflow table | Correct | ✅ |

### `.cursor/skills/plane-radar-adsb/SKILL.md`

| Claim | Reality | Sev |
|---|---|---|
| `services::adsb::fetchUpdate(...)` in the display-integration snippet | `core::adsb::fetchUpdate` | **CRITICAL** |
| API base, NM conversion, 1 req/s, `kMaxAircraft = 64`, `Aircraft` layout, JSON field-mapping table, streaming rationale (`std::bad_alloc`), 512-byte device window, curl replay, `test/test_adsb/`, key-files table | **All correct.** The best-maintained of the eight files | ✅ |

### `.cursor/skills/plane-radar-airport-data/SKILL.md` + `reference.md`

| Claim | Reality | Sev |
|---|---|---|
| Generated from OurAirports; never hand-edit `large_airports_data.cpp`; `type == large_airport`; open runways; `H*` excluded; e7 fixed point; `drawLargeAirportRunways()` in `runway_overlay.cpp`; portal checkbox → `ui::radar::showRunways()`; CSV column tables | **All correct.** No issues found | ✅ |
| Does not mention the ×5 length exaggeration | Cosmetic but user-visible, and the first thing someone comparing to a chart will question | LOW |

### `.cursor/skills/plane-radar-build-flash/SKILL.md`

| Claim | Reality | Sev |
|---|---|---|
| No mention of `--no-stub` or why | See above | **CRITICAL** |
| No mention of closing the serial monitor; the doc actively shows `pio device monitor` right after `pio run -t upload` with no warning | The most common false alarm | **CRITICAL** |
| Troubleshooting: "Display wrong colors → `kDisplayInvert` / `kDisplayRgbOrder` in `config.h`" | `kDisplayInvert` is in `include/platform/device/pins.h`; only `kDisplayRgbOrder` is in `config.h` | HIGH |
| No mention of `Failed to read MISA from hart 0` / power-cycle-only recovery | Looks exactly like dead hardware | HIGH |
| Env table lists only `supermini` | No native/test envs, no `make` targets, though the Makefile is the actual entry point | MED |
| Merge offsets (0x0 / 0x8000 / 0xe000 / 0x10000), release tag flow, CI parity, 115200, 4 MB | Correct | ✅ |

### `.cursor/skills/plane-radar-wifi-portal/SKILL.md`

| Claim | Reality | Sev |
|---|---|---|
| Custom-fields table omits **`show_terrain`** ("Show terrain") | `portal_params.cpp:33-34`. An agent adding a field will not follow the pattern for the newest one | HIGH |
| `planeradar` key list omits **`showTerr`** | `settings.h:64` | HIGH |
| Wipe list: "km/runway prefs (`unitsReset()`)" | `unitsReset()` also resets `showTerr` | MED |
| "Add-a-field" checklist correctly names `wifi_setup_device.cpp` for `kMaxPortalFields` and `appendField()` in `portal_server.cpp` — the only rules/skills file that gets device filenames right | Correct | ✅ |
| `kDoubleTapWindowMs` (500 ms), `kAdsbMinRefetchMs` (1000 ms), portal names, mDNS `-DWM_MDNS`, boot flow, field IDs, `onPortalParamsSaved()` → `commit()`, TX power | Correct | ✅ |

### `README.md`

The README is in **much better shape than `.cursor/`** — it is the only place the memory measurement, the no-FPU measurement, `--no-stub`, and the JTAG-wedge recovery are written down as prose. Treat it as a source, not a target. Issues:

| Line | Claim | Reality | Sev |
|---|---|---|---|
| — | Never mentions closing the serial monitor before upload | Undocumented repo-wide | HIGH |
| 14 | "periodic ADS-B updates (~3 s)" | `kAdsbFetchIntervalMs = 10000` — **10 s** | MED |
| 161 | "Poll interval: `kAdsbFetchIntervalMs` (3 s)" | 10 s. Same error, stated twice | MED |
| 172 | Configuration table: "BOOT → `kBootPin`, …" under "Edit `include/config.h`" | `kBootPin` is in `include/platform/device/pins.h` | MED |
| 173 | "Display SPI → pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz`" under `config.h` | Only `kDisplayRgbOrder` is in `config.h`; pins, `kDisplayInvert` and `kDisplaySpiWriteHz` are in `pins.h`. Add a `pins.h` row and note it is device-only | MED |
| 122 | "a decoder (~36 KB)" | `kScratchBytes` = 35,840; `png_decode.h` says ~44 KB. Pick 35 KB everywhere | LOW |
| 215-218 | Project-layout test note names `native_test_fetch` and `native_test_png` | Also `native_test_live` (correctly listed at line 279) | LOW |

Verified correct in README (spot-checked against source): range preset table 10/20/40/80 NM → 19/37/74/148 km and the outer/fetch radii; `kMaxSites = 6`; BOOT single/double/hold semantics; the terrain section including 41×41/3.4 KB, 1–4 tiles, zoom-per-view, one grid cached, 60 s backoff, Adler-32 rationale; the memory measurement 114,676/115,200 + TIME_WAIT; 209 ms/297 ms vs 37 ms; the sdl2-compat main-thread rule; `--no-stub` rationale; MISA/power-cycle; `pio debug` unusable on Python 3.13+; env list; wiring table.

### Recommended action on `.cursor/`

1. **`plane-radar-project.mdc` — rewrite from scratch.** It is `alwaysApply: true` and wrong on layers, namespaces, envs and the display config path. Once `ARCHITECTURE.md` exists, shrink it to a pointer: *"Read `ARCHITECTURE.md` for structure and `docs/constraints.md` before changing memory, per-pixel arithmetic, storage keys or upload flags"*, plus the correct namespace list. A short correct file beats a long stale one, and at `alwaysApply: true` it pays its token cost on every request.
2. **`esp32-conventions.mdc` — rewrite the NVS table** (three nonexistent filenames, and `useMiles` is the retired key whose reuse is a field-breaking bug). Point the constraints section at `docs/constraints.md#frozen-storage-keys`.
3. **`radar-ui.mdc` — fix the preset values and delete the "no full-screen clear" claim** (both false). Add a no-FPU line pointing at `docs/constraints.md#no-fpu`.
4. **`platformio-ci.mdc` — add `--no-stub`, the monitor-close rule, the full env list, and the `native_test_png` SDL constraint.**
5. **`plane-radar-build-flash` — add `--no-stub`, monitor-close, MISA recovery; fix the `kDisplayInvert` location.**
6. **`plane-radar-wifi-portal` — add `show_terrain` / `showTerr`.**
7. **`plane-radar-adsb` — one-word fix** (`services::` → `core::`).
8. **`plane-radar-airport-data` — no changes needed.**

**Structural recommendation.** All four rules files rotted the same way because each restates facts that live in the code. Once `ARCHITECTURE.md` and `docs/constraints.md` exist, `.cursor/` should **link, not duplicate** — the same discipline this plan applies to source comments. A rule file that says "read `docs/constraints.md#memory-budget`" cannot go stale; one that says "presets are 5, 10, 15, 25 km" already has.

## 10. Suggested execution order

1. **Land `docs/constraints.md`** exactly as drafted in §3. Nothing else moves until every anchor it defines exists.
2. **Decide the §1 deviation** — two extra module docs, or fold into `ARCHITECTURE.md`.
3. **Write `ARCHITECTURE.md` and the module docs** from the §4 outlines, sourcing prose from the comments named in §5 and from `README.md`. Cross-check the three drifted decoder-size numbers against `kScratchBytes` while doing it.
4. **Rewrite `.cursor/rules/plane-radar-project.mdc`** (§9). It is `alwaysApply: true`; leaving it wrong for another day is the largest outstanding risk in the repo.
5. **Close the one TODO** (§8) — a desk-and-a-board job that removes 8 comment lines and a real hazard.
6. **Strip, in this order:** section banners (safe, mechanical) → RESTATES (7 lines, safe) → RATIONALE per §5, file by file, `git diff` per file → leave API CONTRACT and the §7 exceptions alone.
7. **Fix the README items in §9** in the same pass.
8. **Verify:** `make build && make native-build && make test`, then
   `grep -orh 'docs/[a-z/-]*\.md#[a-z0-9-]*\|ARCHITECTURE\.md#[a-z0-9-]*' src include | sort -u`
   and confirm every anchor resolves to a real heading. Add that grep to CI so a renamed heading cannot silently orphan a pointer.
