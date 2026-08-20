# Findings 02 — Memory and RAM Footprint

Scope: static RAM (.bss/.data), heap headroom, stack depth, and buffer sharing on `env:supermini` (ESP32-C3, Arduino 2.0.14 / espressif32 6.5.0, GCC 8.4.0). Read-only — nothing in `src/`, `include/`, `test/`, `scripts/` or `platformio.ini` was touched, and no mutating git or hardware command was run.

## Measured baseline

The build succeeded (`pio run -e supermini`, SUCCESS in 2.27 s), so every number marked **measured** comes from real toolchain output on `.pio/build/supermini/firmware.elf`.

### Section totals

`riscv32-esp-elf-size -A .pio/build/supermini/firmware.elf` — **measured**:

| Section | Bytes | Where it lives |
|---|---:|---|
| `.dram0.data` | 15,516 | internal SRAM (DRAM) |
| `.dram0.bss` | 43,152 | internal SRAM (DRAM) |
| `.noinit` | 0 | — |
| **static DRAM total** | **58,668** | matches PlatformIO's `RAM: 17.9% (used 58668 bytes from 327680)` |
| `.dram0.dummy` | 61,952 | DRAM *alias* reserved for `.iram0.text` — real SRAM cost, invisible in PlatformIO's RAM line |
| `.iram0.text` (+`_end`) | 61,952 | instruction RAM |
| `.flash.text` | 914,680 | flash (XIP) |
| `.flash.rodata` | 272,584 | flash (mmap at `0x3C0E0120`) |
| `.eh_frame` | 67,648 | flash — C++ unwind tables |
| `.flash.rodata_noload` | 16,599 | flash, not loaded |
| `.rtc*` | 32 | RTC RAM |

`pio run -e supermini -t size` — **measured**: `Flash: 40.2% (used 1264250 bytes from 3145728)`. `firmware.bin` = 1,332,256 bytes on disk.

### Heap window

From `firmware.map` — **measured**:

```
_static_data_end = _bss_end = 0x3fc9d730
_heap_start                 = 0x3fc9d730      (identical address)
```

`dram0_0_seg` in `memory.ld` is `org = 0x3FC80000`, computed `len = 0x4E710`, ending at `0x3FCCE710`.

* Static SRAM consumed before the heap: `0x3FC9D730 − 0x3FC80000` = **120,624 bytes** = 61,952 (IRAM alias) + 15,516 (`.data`) + 43,152 (`.bss`) + 4 align.
* Link-time heap window: `0x3FCCE710 − 0x3FC9D730` = **200,672 bytes** — a ceiling, before WiFi/lwIP/mbedTLS init, the FreeRTOS task stacks and the 115,200-byte frame sprite draw from it.

**The load-bearing consequence, measured not assumed:** `_heap_start == _bss_end`. Every byte removed from `.bss` or `.data` becomes one more byte of heap, 1:1. That is the mechanism by which all the `.bss` items below buy TLS headroom.

* **Free heap at runtime: UNMEASURED.** It cannot be derived statically. The prior session's figures (~45 KB free with the sprite live, largest free block 114,676) are consistent with the arithmetic above but were not reproduced; no device was used.

### Frame sprite and PNG scratch — measured

* Frame sprite: `240 × 240 × 2` = **115,200 bytes**, heap-allocated once in `ensureFrameSprite()` (`src/ui/radar_display.cpp:629`) and never freed. The `LGFX_Sprite` *object* is 344 bytes of `.bss` (DWARF).
* `sizeof(platform_png::Work)` = **35,520 bytes** (DWARF `DW_AT_byte_size`). `kScratchBytes` = 35,840 → **320 bytes of slack** (see MEM-09).
* Unused sprite scratch during a tile decode: 115,200 − 35,840 = **79,360 bytes**.

### TLS cost per request — measured from framework config

`framework-arduinoespressif32/tools/sdk/esp32c3/sdkconfig`:

```
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
# CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN is not set
# CONFIG_MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH is not set
```

`mbedtls/ssl_internal.h`: `IN_BUFFER_LEN = HEADER_LEN(13) + PAYLOAD_OVERHEAD(0 + 16 IV + 48 MAC + 256 padding + 0) + IN_CONTENT_LEN(16384)` = **16,717 bytes**, same for `OUT_BUFFER_LEN`. So the two record buffers alone are **33,434 bytes of heap per TLS session**, plus (DWARF-measured) `sslclient_context` 2,200, `mbedtls_ssl_context` 544, `mbedtls_ssl_config` 232, plus the transient handshake struct. That is the ~30 KB in the header comments — now with a source.

`mbedtls_ssl_conf_max_frag_len` **is** linked (present in `libmbedtls_2.a`), but `MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH` is compiled **off** (`esp_config.h:1363-1366`), so negotiating a smaller fragment does not shrink the allocation.

### Our static footprint vs the framework's — measured

Aggregated from `firmware.map` input sections:

| Owner | `.dram0.bss` | `.dram0.data` |
|---|---:|---:|
| `src/` (ours) | **15,044** | **75** |
| framework / libs | 28,108 | 15,441 |

Ranked `.bss` consumers in `src/` (`firmware.map`, per input section — **measured**):

| Bytes | Symbol | File |
|---:|---|---|
| 3,392 | `core::terrain::s_grid` | `src/core/terrain.cpp:38` |
| 3,072 | `core::adsb::s_aircraft` | `src/core/adsb.cpp:19` |
| 1,166 | `ui::runway::s_label_pending` | `src/ui/runway_overlay.cpp:21` |
| 1,166 | `ui::runway::s_in_range` | `src/ui/runway_overlay.cpp:20` |
| 960 | `ui::terrain::s_frac` | `src/ui/terrain_overlay.cpp:40` |
| 960 | `ui::terrain::s_cell` | `src/ui/terrain_overlay.cpp:39` |
| 768 | `s_attrs` (portal HTML attrs) | `src/platform/device/wifi_setup_device.cpp:86` |
| 620 | `s_wm` (`WiFiManager` object) | `src/platform/device/wifi_setup_device.cpp:64` |
| 528 | `tft` | `src/platform/device/display_device.cpp` |
| 512 | `core::terrain::s_row_first` | `src/core/terrain.cpp:105` |
| 512 | `core::terrain::s_col_first` | `src/core/terrain.cpp:106` |
| 344 | `ui::s_frame` (sprite object) | `src/ui/radar_display.cpp:57` |
| 164 | `core::terrain::s_row_py` | `src/core/terrain.cpp:84` |
| 164 | `core::terrain::s_col_px` | `src/core/terrain.cpp:83` |
| 120 | `s_spinner_dots` | `src/ui/status_screens.cpp:37` |
| 104 | `core::terrain::s_prog` | `src/core/terrain.cpp:72` |
| 82 + 82 | `s_row_local`, `s_col_local` | `src/core/terrain.cpp:92-93` |
| 36 + 21 + 21 | `s_pending_sites`, `s_pending_lat/lon` | `src/core/portal_params.cpp:37-39` |
| 30 | `core::settings::s_site_idents` | `src/core/settings.cpp:39` |
| 14 | `ui::radar::kColorTerrain` | `src/ui/radar_display.cpp:34` |

Largest framework `.bss` blocks, for context: `libnet80211 wl_cnx.o` 3,855, `ieee80211_ftm.o` 2,844, `freertos/port.c` 2,116, `libmdns` 1,894, `liblwip/dns.c` 1,323, `libespcoredump` 1,132 + `s_coredump_stack` 1,124, `liblwip/sockets.c` 1,032.

### Flash `.rodata` — is `large_airports_data.cpp` in RAM? No. Measured.

| Bytes | Object |
|---:|---|
| **59,600** | `src/core/large_airports_data.cpp.o` |
| 26,495 | `libWiFiManager.a(WiFiManager.cpp.o)` |
| 25,921 | `LovyanGFX lgfx_fonts.cpp.o` |
| 11,010 | `libmbedcrypto.a(error.c.obj)` |
| 10,766 | `ui_font.vlw.txt.o` (embedded VLW) |
| 10,221 | `libmbedtls_2.a(ssl_ciphersuites.c.obj)` |
| 8,276 | `libWebServer.a(WebServer.cpp.o)` |
| 7,543 | `esp_err_to_name.c.obj` |

`nm` — **measured**: `data::large_airports::kRunways` = 40,944 bytes at `0x3C0EA124`, `kAirports` = 18,656 bytes at `0x3C0F41D4`. Both inside `.flash.rodata` (`0x3C0E0120`…`0x3C1228A8`). `large_airports_data.cpp.o` contributes **0 bytes** to `.dram0.data` and **0 bytes** to `.dram0.bss`; 40,944 + 18,656 = 59,600 exactly accounts for its `.rodata`. **Not copied to RAM. Zero RAM cost.** (`nm` types them `D` rather than `R` only because `.flash.rodata` isn't a name it recognises as read-only; the address settles it.)

### Stack — measured frame sizes

Frames from `objdump -d` prologues (`addi sp,sp,-N`, max per function). Task stack: `CONFIG_ARDUINO_LOOP_STACK_SIZE=8192`. `FrameworkArduino/main.cpp.o` **is** compiled from source in this build, so `-DARDUINO_LOOP_STACK_SIZE=` in `build_flags` would take effect.

| Frame | Function |
|---:|---|
| **1,984** | `ui::(anonymous)::drawAircraft()` |
| 896 | `core::platform::HttpClient::get(...)` |
| 656 | `start_ssl_client(...)` |
| 528 | `platform_png::decode(...)` |
| 336 | `core::adsb::parseBody(...)` |
| 320 | `core::platform::logf(...)` |
| 304 | `core::terrain::ensureGrid(...)` |
| 256 | `ui::terrain::drawTerrainBackground(...)` |
| 192 | `ui::runway::drawLargeAirportRunways(...)` |
| 1,168 / 784 | `mbedtls_rsa_rsassa_pss_verify_ext` / `mbedtls_internal_sha512_process` |
| 1,072 | `_svfprintf_r` (newlib float printf) |

Whole-graph worst chains (direct `jal`/`tail` edges only; indirect/virtual calls are invisible to this method, so these are **lower bounds**):

* **Render path:** `loopTask` 16 → `loop()` 48 → `radarDisplayDraw()` 48 → `renderFrame()` 144 → **`drawAircraft()` 1,984** → `initLabelMetrics()` 80 → `formatRing3Label` 16 → `snprintf` 176 → `_svfprintf_r` 1,072 → … = **3,904 bytes** of 8,192.
* **TLS handshake path** (stitched by hand across the virtual `WiFiClient::connect` edge the graph loses): `loop` 48 + `ensureGrid` 304 + `HttpClient::get` 896 + `start_ssl_client` subtree 3,536 ≈ **4,800 bytes**.
* **PNG decode path:** `ensureGrid` 304 + `HttpClient::get` 896 + `decode` subtree 2,432 (of which 1,824 is a `logf` reaching `_svfprintf_r`) ≈ **3,632 bytes**.
* **No recursion** on any of our paths. The only cycles found are the `__cxa_*`/`_Unwind_*` cluster and lwIP's `netconn_free`.

Stack locals over 256 bytes — **measured**:

| Bytes | Local | Location |
|---:|---|---|
| 1,024 | `AircraftDrawItem items[64]` (16 B × 64) | `src/ui/radar_display.cpp:422` |
| 768 | `BeyondDotDrawItem dots[64]` (12 B × 64) | `src/ui/radar_display.cpp:423` |
| 544 | `StreamBodyReader body` (incl. `char buffer_[512]`) | `src/platform/device/http_arduino.cpp:132,164` |
| 320 | `uint8_t lengths[288+32]` | `src/platform/png_decode.cpp:431` |
| 288 | `uint8_t lengths[288]` | `src/platform/png_decode.cpp:388` |
| 256 | `char buf[256]` in `logf` | `src/platform/device/platform_device.cpp:26` |
| 164 | `int32_t row_elev[41]` | `src/ui/terrain_overlay.cpp:84` |
| 160 | `char url[160]` | `src/core/terrain.cpp:433`, `src/core/adsb.cpp:284` |
| 64 | `char sink[64]` | `src/platform/png_decode.cpp:224` |

One extra hazard, **measured**: `CONFIG_COMPILER_CXX_EXCEPTIONS=y` and `compile_commands.json` shows `-fexceptions` (157 TUs, zero `-fno-exceptions`). The unwinder is live: `_Unwind_RaiseException` 1,424 + `_Unwind_RaiseException_Phase2` 624 + `uw_update_context_1` 432 + `execute_stack_op` 320 + `read_encoded_value` 48 ≈ **2,864 bytes**. Also `CONFIG_COMPILER_CXX_EXCEPTIONS_EMG_POOL_SIZE=0`, so a `throw` when the heap is exhausted cannot allocate its exception object and lands in `std::terminate`. A `std::bad_alloc` from `operator new` inside the TLS path (e.g. `new sslclient_context`, 2,200 bytes, in `WiFiClientSecure`'s constructor) therefore either aborts outright or needs ~2.9 KB of stack on top of a ~4.8 KB chain.

### Correction to the project facts handed to this investigation

`data::large_airports` **does** exist as a real namespace (`include/core/large_airports.h:7`, generated). The brief said `data::*` does not exist; true of `data::` in general, not of this one.

## Ranked opportunities

"heap" = frees DRAM that `_heap_start` reclaims 1:1; "stack" = reduces peak `loopTask` depth only.

| ID | Est. bytes saved | Confidence | file:line | Idea |
|---|---:|---|---|---|
| MEM-01 | 2,324 heap | High | `src/ui/runway_overlay.cpp:20-21` | Delete both `bool[1166]` arrays; `kRunways` is generated grouped by `airport_idx`, so two scalars suffice |
| MEM-02 | 1,920 heap | High | `src/ui/terrain_overlay.cpp:39-40` | The pixel→grid LUTs are pure functions of compile-time constants — make them `constexpr` so they live in flash |
| MEM-03 | 14,336 heap per TLS session | Medium | framework `sdkconfig` | Enable `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN` with `OUT_CONTENT_LEN=2048`; needs an Arduino-as-IDF-component build |
| MEM-04 | 1,024 stack | High | `src/ui/radar_display.cpp:422-423` | `items[]` and `dots[]` are provably disjoint — one 64-entry array filled from both ends |
| MEM-06 | 512 heap | High | `src/core/terrain.cpp:105-106` | `s_row_first`/`s_col_first` hold −1…40; `int16_t` → `int8_t` |
| MEM-08 | 1,152 heap + 384 stack | Medium | `include/core/aircraft.h:25` | `kMaxAircraft` 64 → 40 |
| MEM-07 | ~5,000 heap | Low-Med | `src/platform/device/wifi_setup_device.cpp:428-441` | Stop starting the LAN web portal + mDNS unconditionally from `wifiLoop()`; make them on-demand |
| MEM-05 | up to 2,048 heap | Medium | `platformio.ini` `build_flags` | Trim `ARDUINO_LOOP_STACK_SIZE` from 8192 — only after a high-water-mark measurement, and only after MEM-04 |
| MEM-10 | 1,188 heap | Medium | `src/core/terrain.cpp:92-93,105-106` | Move the per-tile resample tables into the 79,360 unused bytes of borrowed sprite scratch |
| MEM-13 | 512 heap + 320 stack | Medium | `include/core/aircraft.h:13-22` | Pack `Aircraft` from 48 → 40 bytes |
| MEM-11 | 34 heap | High | `src/ui/radar_display.cpp:24-34` | `color565` is a pure bit-shift — make the whole palette `constexpr` |
| MEM-14 | 128 stack | High | `src/platform/device/platform_device.cpp:26` | `logf`'s `char buf[256]` → 128 |
| MEM-12 | 5,696 heap | Low | framework `sdkconfig` | `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y` costs ~5.7 KB of static RAM |
| MEM-09 | 0 | High | `include/platform/png_decode.h:44` | `kScratchBytes` has 320 bytes of slack, but shrinking it saves nothing. Its *documented* breakdown is wrong |

In-tree, framework-free total: **MEM-01 + 02 + 06 + 11 = 4,790 bytes of heap** at essentially no risk; **+ MEM-08 + MEM-10 + MEM-13 = 7,642 bytes** with modest risk. Against the ~13 KB shortfall the header comments describe, the zero-risk set closes ~37 % and the full in-tree set ~59 %. MEM-03 closes it outright.

### MEM-01 — Delete both 1,166-byte `bool` arrays in the runway overlay

- **Location:** `src/ui/runway_overlay.cpp:20-21`, used only in `drawLargeAirportRunways()` at `:267-294`
- **Current cost:** 1,166 + 1,166 = **2,332 bytes of `.bss`**, measured as two separate input sections in `firmware.map` (`.bss._ZN2ui6runway12_GLOBAL__N_110s_in_rangeE`, `…15s_label_pendingE`). Live for the whole program but *used* only inside one function, which resets them to `false` on every call.
- **Proposal:** `kRunways` is generated grouped and sorted by `airport_idx` — verified by parsing `src/core/large_airports_data.cpp`: 1,706 rows, `airport_idx` non-decreasing, 1,113 distinct groups, **0 non-contiguous re-entries** — guaranteed by `scripts/build_large_airports.py:119` (`segments.sort(key=lambda row: (row[0], -row[5]))`). So the loop only needs the *current* group: `uint16_t cur_ap = 0xFFFF; bool cur_in_range; bool cur_labelled;`. `s_label_pending` is redundant even without the grouping property — `label_airports[]` is at most `kMaxAirportLabels` = 32 entries, so "already queued?" is a ≤32-element scan. Because the invariant comes from a generated file, the generator should assert the grouping and emit it as a `constexpr bool` (or the loop should treat `ap_idx < cur_ap` as a hard fallback) so a future dataset change cannot silently break the overlay.
- **Estimated saving:** **2,324 bytes** (2,332 − 8). High confidence: counts read off the map file, grouping verified against the actual generated data.
- **Risk:** None to the borrowed-scratch invariant, TLS headroom or sprite lifetime — nothing here runs during a download. The only risk is the dataset ordering invariant, hence the generator-side assert. Fallback with no ordering dependency at all: bit-pack both arrays to `uint8_t[147]`, saving 2,038 bytes.
- **Verification:** `pio run -e supermini -t size` should drop `.dram0.bss` by ~2,332 (fills may shift a few bytes); `nm` should no longer list either symbol. On device, the overlay must still draw every in-range runway and exactly one label per airport at each of the four range presets.

### MEM-02 — Make the terrain pixel→grid LUTs `constexpr` (move 1,920 B to flash)

- **Location:** `src/ui/terrain_overlay.cpp:39-40`, filled by `initPixelToGridMap()` at `:43-62`
- **Current cost:** `int s_cell[240]` 960 + `int32_t s_frac[240]` 960 = **1,920 bytes of `.bss`**, measured as `.bss._ZN2ui7terrain12_GLOBAL__N_16s_cellE` / `…6s_fracE`. Live for the whole program, written once.
- **Proposal:** `initPixelToGridMap()` reads nothing but `radar::kSize` (240) and `kGrid` (41), both `constexpr`. Wrap the tables in a `constexpr` struct returned by a `constexpr` function (fine under `gnu++17`/GCC 8.4) so both land in `.flash.rodata`; drop `s_map_ready` and the call site at `:131`. Narrow the element types too: `s_cell` ranges 0…39 (`uint8_t`), `s_frac` 0…256 (`uint16_t`), so flash cost is 240 + 480 = 720 bytes. `bandAtPixel()` is unaffected — `row_elev[]` stays `int32_t` and `int32 × uint16` promotes to `int32`.
- **Estimated saving:** **1,920 bytes of `.bss`** → heap; +720 bytes of flash (0.02 % of the partition). High confidence.
- **Risk:** None. Not on the download path; touches neither scratch, TLS nor the sprite. It also removes a lazy-init branch from the per-frame path — a side benefit, not the point.
- **Verification:** `.dram0.bss` drops 1,920, `.flash.rodata` rises ~720; `nm` no longer lists `s_cell`/`s_frac` in `.bss`. Terrain background must be pixel-identical — `make native` is the cheap check, since both destinations compile this file.

### MEM-03 — Shrink the mbedTLS *outgoing* record buffer (asymmetric content len)

- **Location:** framework config, not our tree: `framework-arduinoespressif32/tools/sdk/esp32c3/sdkconfig:1425-1426`
- **Current cost:** **measured** — `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN` not set, so both `MBEDTLS_SSL_IN_BUFFER_LEN` and `MBEDTLS_SSL_OUT_BUFFER_LEN` evaluate to `13 + 320 + 16384` = **16,717 bytes each**, i.e. **33,434 bytes of heap per TLS session**, held for the whole request. The single largest transient heap consumer in the firmware and the direct cause of the "SSL - Memory allocation failed" class of failure.
- **Proposal:** Set `CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y`, leave `MBEDTLS_SSL_IN_CONTENT_LEN` at 16384, set `MBEDTLS_SSL_OUT_CONTENT_LEN` to 2048. Outgoing records are entirely under our control — the firmware sends one short `GET` — so capping the *out* buffer carries no protocol risk, unlike shrinking the *in* buffer (a server may legally send 16 KB records; `mbedtls_ssl_conf_max_frag_len` cannot help because `CONFIG_MBEDTLS_SSL_VARIABLE_BUFFER_LENGTH` is off — measured at `sdkconfig:1432` and `esp_config.h:1363-1366` — so a negotiated MFL does not change the allocation). Cost is the build: `libmbedtls_2.a` ships precompiled, so this needs the Arduino-as-ESP-IDF-component flow (or a forked framework package). No `-D` in `build_flags` can reach it.
- **Estimated saving:** **14,336 bytes** of heap for the duration of every request (16,384 − 2,048). The arithmetic is certain; Medium confidence only because it moves the toolchain off the pinned `espressif32@6.5.0` Arduino path.
- **Risk:** Does not touch borrowed scratch or sprite lifetime — it *makes* TLS headroom. The risk is build-system: a framework rebuild changes every other prebuilt library, so `docs/fidelity-baseline.txt` must be re-checked.
- **Verification:** Log `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` and `esp_get_free_heap_size()` immediately before and after `platform::HttpClient::get()`; the dip during a request should shrink by ~14 KB. Then confirm N consecutive terrain grids (4 tiles each) complete with no "SSL - Memory allocation failed" and no `png: no scratch available`.

### MEM-04 — Merge `items[]` and `dots[]` into one draw array

- **Location:** `src/ui/radar_display.cpp:422-423`, populated at `:431-455`
- **Current cost:** `sizeof(AircraftDrawItem)` = 16 and `sizeof(BeyondDotDrawItem)` = 12 (both **measured** from DWARF), × 64 = 1,024 + 768 = **1,792 bytes of stack**. Measured frame for `drawAircraft()` is **1,984 bytes** — the largest single frame anywhere in our code, on the deepest render chain (3,904 bytes measured).
- **Proposal:** The classification loop at `:431-455` puts each aircraft in *exactly one* list: it either pushes to `items` and `continue`s, or pushes to `dots`, or drops the target. So `draw_count + dot_count ≤ n ≤ kMaxAircraft`. One array of `kMaxAircraft` entries, filled from index 0 upward for ring targets and from the top downward for rim dots, holds both with no possible overlap; both lists stay readable simultaneously (dots drawn at `:457-460`, items at `:462-481`), which the two-ended fill preserves. Narrow the fields too: `index` needs 6 bits (`uint8_t`), `x`/`y` fit `int16_t`, `dist_sq` peaks at 240² × 2 = 115,200 so it needs `int32_t` → 12-byte element, 64 × 12 = 768 bytes. If the sort key is narrowed to `uint16_t (dist_sq >> 4)` — the sorts at `:392-414` only need a monotonic paint order, and the ties introduced are between pixels 4 apart — the element drops to 8 bytes → 512.
- **Estimated saving:** **1,024 bytes of stack** for the 12-byte element (high confidence, arithmetic on DWARF sizes), or **1,280 bytes** for the 8-byte element (same arithmetic, but the narrowed sort key is a behaviour change I have not verified visually).
- **Risk:** None to scratch, TLS or the sprite — `drawAircraft()` never runs during a download. The invariant is local to one loop, airtight and cheap to re-check.
- **Verification:** Re-run the frame extraction (`objdump -d | grep -A1 '<ui::.*drawAircraft'`) and confirm the prologue's `addi sp,sp,-N` dropped by ~1,024. On device, log `uxTaskGetStackHighWaterMark(nullptr)` from `loop()` and confirm the high-water mark improves by roughly the same amount with 40+ aircraft on screen. Paint order (far behind near) must be unchanged.

### MEM-06 — `s_row_first`/`s_col_first`: `int16_t` → `int8_t`

- **Location:** `src/core/terrain.cpp:105-106`
- **Current cost:** `int16_t[256]` × 2 = **1,024 bytes of `.bss`**, measured as two 512-byte input sections. Live for the whole program, used only between `beginTile()` (`:146`) and the end of the tile's decode.
- **Proposal:** These store a grid row/column index or −1. `kGridSize` is 41 (`config.h:66`), so the range is −1…40 and `int8_t` suffices with room to spare. `memset(..., -1, ...)` at `:149-150` already works byte-wise, so no change there. Add `static_assert(kGridSize <= INT8_MAX)` beside the declarations so a future grid-size bump cannot silently truncate.
- **Estimated saving:** **512 bytes** of `.bss` → heap. High confidence.
- **Risk:** None. Same lifetime, same semantics; the only failure mode is `kGridSize` growing past 127, which the `static_assert` catches at compile time. This is the smaller, zero-effort alternative to part of MEM-10.
- **Verification:** `.dram0.bss` drops 512; `nm --print-size` shows both symbols at 256 bytes. On device, grids must still complete with `terrain: grid ready` and never `only N/1681 samples decoded`. Host tests cover this: `pio test -e native_test_fetch` exercises the resampler, `make test-live` decodes real tiles.

### MEM-08 — `kMaxAircraft` 64 → 40

- **Location:** `include/core/aircraft.h:25`; storage at `src/core/adsb.cpp:19`
- **Current cost:** `sizeof(core::adsb::Aircraft)` = 48 (DWARF-measured; 5 floats = 20 + `callsign[9]` + `type[5]` + `alt[12]` = 46, padded to 48) × 64 = **3,072 bytes of `.bss`**. Permanently live. It also sizes the two stack arrays in MEM-04.
- **Proposal:** Drop the cap to 40. `parseBody()` at `src/core/adsb.cpp:243` already handles overflow gracefully (stops reading, lets the transport close), and the render path already collapses tags to one line above 4 *tagged* aircraft (`radar_theme.h:89`), so the marginal value of slots 41-64 is a few extra rim dots.
- **Estimated saving:** **1,152 bytes** of `.bss` → heap, plus 384 bytes of stack once MEM-04 lands (24 fewer × 16 B). Byte count is certain; whether 40 is enough is a product judgement I cannot measure — Medium confidence overall.
- **Risk:** None to scratch/TLS/sprite. The behavioural risk is real and observable: at an 80 NM ring over a dense metro, adsb.fi can return well over 40 targets and the surplus would silently vanish. Decide against a logged distribution, not an assumption.
- **Verification:** Log the existing `adsb: %u aircraft` line over a busy period at the 80 NM preset and check how often it saturates. `.dram0.bss` should drop 1,152.

### MEM-07 — Stop running the LAN web portal and mDNS unconditionally

- **Location:** `src/platform/device/wifi_setup_device.cpp:428-441` (`wifiLoop()` → `startLanWebPortal()` at `:239-256`); the object at `:64`
- **Current cost:** partly measured, partly estimated. Measured: `s_wm` (`WiFiManager`) = 620 bytes `.bss` (DWARF), `s_attrs` = 768 bytes `.bss`, `libmdns` = 1,894 bytes `.bss`, `CONFIG_MDNS_TASK_STACK_SIZE=4096`, `sizeof(WebServer)` = 284 (DWARF). `WiFiManager::startWebPortal()` reaches `server.reset(new WM_WebServer(80))` (`WiFiManager.cpp:630`), so the WebServer, its listening socket and its `String` members are heap-resident from the moment WiFi comes up until reboot. The mDNS task's 4,096-byte stack is `xTaskCreate`d from the heap. **Total heap held: UNMEASURED**, estimated ~5 KB (4,096 task stack + ~100 TCB + ~284 WebServer + sockets/`String`s/mdns server struct).
- **Proposal:** Two separable changes. (a) Do not call `startLanWebPortal()` from `wifiLoop()`; start it on an explicit gesture (e.g. a triple tap or a portal flag in NVS) and `stopLanWebPortal()` after an idle timeout — that releases the WebServer, its socket and the mDNS task. (b) Independently: `wifiLoop()` is also the `PollFn` handed to `core::adsb::setPollFn`/`core::terrain::setPollFn` (`src/main.cpp:149-150`), so `s_wm.process()` runs *inside* the TLS request loop at `http_arduino.cpp:37,103`. A browser hitting the LAN portal while a tile downloads allocates `String`s and lwIP buffers in exactly the window where the heap is at its minimum. At minimum, gate `s_wm.process()` out of the poll path while a request is in flight (`core::terrain::downloadActive()` already exposes the state) — costs nothing, removes a genuine allocation race.
- **Estimated saving:** **~5,000 bytes of heap**, Low-Medium confidence — the 4,096-byte mDNS task stack is hard, the rest estimated. Sub-change (b) saves no bytes but removes a concurrent allocator from the critical window.
- **Risk:** No interaction with borrowed scratch or sprite lifetime; it *adds* TLS headroom. The risk is functional: the LAN config page stops being always-on, which is a documented feature (the `plane-radar.local` flow). (b) has a subtler risk: while a request is in flight the portal stops responding, so a browser request could time out — acceptable for a ≤10 s request, but a behaviour change.
- **Verification:** Log `esp_get_free_heap_size()` at the end of `setup()` with and without the portal started; the delta is the saving. Then confirm the config page is still reachable after the trigger gesture, and that `MDNS.begin` is not called on the happy path.

### MEM-05 — Trim `ARDUINO_LOOP_STACK_SIZE`, but only after measuring

- **Location:** `platformio.ini` `[env:supermini] build_flags`; default from `CONFIG_ARDUINO_LOOP_STACK_SIZE=8192`
- **Current cost:** **8,192 bytes of heap**, `xTaskCreateUniversal`d by the Arduino core. `.pio/build/supermini/FrameworkArduino/main.cpp.o` exists, so the core is compiled from source and `-DARDUINO_LOOP_STACK_SIZE=N` in `build_flags` overrides `CONFIG_ARDUINO_LOOP_STACK_SIZE` at `FrameworkArduino/main.cpp:12-16` — **verified**, not assumed.
- **Proposal:** Deepest chains measured are 3,904 (render) and ~4,800 (TLS handshake). After MEM-04 removes ~1,024 from the render frame, both sit near 4,800. Dropping to 6,656 keeps ~1,850 bytes of margin and returns 1,536 bytes to the heap; 6,144 returns 2,048 with ~1,340 margin.
- **Estimated saving:** up to **2,048 bytes of heap**. Medium confidence in the number, lower in the safety margin: my call-graph walk sees only direct `jal`/`tail` edges, so every indirect call (LovyanGFX virtuals, `WiFiClient::connect`, the `BodyFn`/`PixelFn`/`PollFn` seams) is invisible. The measured depths are lower bounds.
- **Risk:** A stack overflow in the TLS path is a hard crash, and the C++ unwinder adds ~2,864 bytes if a `bad_alloc` is ever thrown from depth. **Do not do this on the strength of the static estimate.** Gate it on `uxTaskGetStackHighWaterMark(nullptr)` sampled at the end of `loop()` across a full terrain download, a busy ADS-B frame and a portal session. No effect on scratch or sprite lifetime.
- **Verification:** Log the high-water mark every loop for an hour of normal operation including several terrain grids; keep at least 2 KB unused at the observed minimum before committing a smaller size. Cross-check that `esp_get_free_heap_size()` rose by exactly the delta.

### MEM-10 — Park the per-tile resample tables in the unused sprite scratch

- **Location:** `src/core/terrain.cpp:92-93` (`s_col_local`, `s_row_local`) and `:105-106` (`s_col_first`, `s_row_first`)
- **Current cost:** 82 + 82 + 512 + 512 = **1,188 bytes of `.bss`**, all measured. Live for the whole program.
- **Proposal:** These four are written by `beginTile()` (`:146-178`), read only by `onPixel()` (`:186-212`), and dead the moment `platform::HttpClient::get()` returns at `:438`. That is *exactly* the interval during which `platform_png::decode()` owns the frame sprite, of which **79,360 bytes are idle** (measured). Extend the existing seam — `core::terrain` already uses platform-injected function pointers (`setPngDecoder`, `setPollFn`), so adding `core::terrain::setScratch()` wired to `ui::radarDisplayFrameScratch` in `src/main.cpp` keeps `core/` free of LovyanGFX. Ask for `kScratchBytes + 1188` and take the tail.
- **Estimated saving:** **1,188 bytes** of `.bss` → heap. Medium confidence in the design cost, high in the byte count.
- **Invariant this depends on, stated explicitly:** the tables are borrowed from the same buffer the PNG decoder borrows, and only for the duration of one `HttpClient::get()` call. Two things must hold. (1) The two borrowers must not overlap — hand the decoder `[scratch, scratch+35840)` and terrain `[scratch+35840, scratch+37028)`; the sprite is 115,200 bytes so both fit with 78 KB spare. (2) Nothing may repaint the sprite between `beginTile()` and the return of `HttpClient::get()`. Today that holds: the poll function (`pollWifiAndTaps`, `src/main.cpp:88`) only calls `wifiLoop()` and queues a tap — it never calls `radarDisplayDraw()`; the tap is consumed later by `handleBootButton()` back in `loop()`. **That is the fragile part.** If a future change lets the poll path repaint (a status overlay, a progress bar), the decoder's window *and* these tables are corrupted together. A comment is not enough; the poll seam should be documented as "must not draw". The cheaper 80 % of this win is MEM-06 (512 bytes, no invariant at all) — prefer that first and take MEM-10 only if the remaining 676 bytes matter.
- **Risk:** The one item that touches the borrowed-scratch invariant. It does not touch sprite *lifetime* (still allocated once, never freed), and it improves TLS headroom.
- **Verification:** `.dram0.bss` drops 1,188. On device, run ≥20 terrain grids at all four presets and confirm every one logs `terrain: grid ready` with no `only N/1681 samples decoded` — a corrupted `s_row_first` shows up as exactly that short count, or as visibly banded/striped terrain. Tapping BOOT mid-download must not corrupt a grid. `make test-live` exercises the real decoder against real tiles and should stay green.

### MEM-13 — Pack `core::adsb::Aircraft` from 48 to 40 bytes

- **Location:** `include/core/aircraft.h:13-22`
- **Current cost:** 48 bytes measured (DWARF); payload is 46, so 2 bytes are pure tail padding. 64 slots = 3,072 bytes of `.bss`.
- **Proposal:** `nose_deg`, `track_deg` and `gs_knots` are floats only because the JSON gives floats; the render path rounds them anyway (`radar_display.cpp:247-267` via `lroundf`). Store heading as `uint16_t` tenths-of-a-degree (0…3600) and `gs_knots` as `uint16_t` knots, and shrink `alt[12]` to `alt[11]` (the widest value the formatter at `adsb.cpp:101` can emit is `"-32000 ft"`, 9 chars + NUL). Layout becomes `float lat, lon` (8) + three `uint16_t` (6) + `callsign[9]` + `type[5]` + `alt[11]` (25) = 39 → 40 with alignment.
- **Estimated saving:** **512 bytes** of `.bss` (64 × 8) plus, with MEM-04, proportional stack. Medium confidence: the byte count is arithmetic, but I have not checked whether any consumer needs sub-0.1° heading precision (nothing in `radar_display.cpp` appears to).
- **Risk:** None to scratch/TLS/sprite. Touches the ADS-B → render contract, so it needs `pio test -e native_test` to confirm the parse and geometry are unchanged.
- **Verification:** `.dram0.bss` drops 512; aircraft symbols, track vectors and tags must be pixel-identical in the native harness.

### MEM-11 — Make the RGB565 palette `constexpr`

- **Location:** `src/ui/radar_display.cpp:24-34`, computed by `initPalette()` at `:179-206`
- **Current cost:** measured — `ui::radar::kColorTerrain` = 14 bytes `.bss`, plus nine `uint16_t` globals in `.dram0.data` (18 bytes, `nm` addresses `0x3FC8F268`…`0x3FC8F278`) and `kColorBackground` 2 bytes in `.sbss`. **34 bytes** of RAM.
- **Proposal:** `LGFXBase::color565(r,g,b)` is `(r>>3)<<11 | (g>>2)<<5 | b>>3` — a pure expression over the `constexpr uint8_t` tables in `radar_theme.h:92-135`. Replace the ten runtime globals with `constexpr uint16_t` values (and the R/B swap at `:185-191` with a `constexpr` branch on `config::kDisplayRgbOrder`) so they live in flash. `initPalette()` and its three call sites (`:620`, `:658`, `:676`) disappear.
- **Estimated saving:** **34 bytes** of RAM. High confidence, and honestly negligible — listed only because it is free and it also removes a redundant recomputation from the per-frame path (`drawStaticGrid` calls `initPalette()` every frame).
- **Risk:** None.
- **Verification:** Colours identical on the panel and in the native harness; the `docs/fidelity-baseline.txt` comparison covers it.

### MEM-14 — `logf`'s stack buffer 256 → 128

- **Location:** `src/platform/device/platform_device.cpp:26`
- **Current cost:** `char buf[256]`; measured frame for `core::platform::logf` is **320 bytes**, and its subtree reaches 1,824 because `vsnprintf` pulls in `_svfprintf_r` (1,072). `logf` is called from `Decoder::emit()` on the PNG decode path (measured edge in the disassembly), so it sits inside the deepest TLS-adjacent chain.
- **Proposal:** Drop to 128. The longest format string in the tree is `"terrain: only %d/%d samples decoded — discarding\n"` (`terrain.cpp:469`), which expands to well under 80 characters.
- **Estimated saving:** **128 bytes of stack** on every logging path. High confidence.
- **Risk:** None to scratch/TLS/sprite. Truncated diagnostics if a future format string grows — cheap to notice.
- **Verification:** `objdump -d` prologue for `logf` drops to ~192; serial output unchanged for all existing messages.

### MEM-12 — Core-dump-to-flash costs ~5.7 KB of static RAM

- **Location:** framework `sdkconfig:1093-1097` (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, `..._DATA_FORMAT_ELF=y`); the `coredump` partition exists at `partitions/plane_radar.csv:0x3F0000`
- **Current cost:** **measured** from `firmware.map`: `libespcoredump.a` supplies 2,400 (`core_dump_elf.c`) + 820 (`core_dump_flash.c`) + 220 (`core_dump_port.c`) = 3,440 bytes of `.dram0.data`, plus 1,132 bytes of `.dram0.bss`, plus `s_coredump_stack` = 1,124 bytes of `.bss`. **≈ 5,696 bytes** of permanently resident SRAM.
- **Proposal:** Disable core dump if post-mortem `.elf` dumps are not part of the debugging workflow. Same constraint as MEM-03: the setting lives in the precompiled framework, so it needs an Arduino-as-IDF-component build. If MEM-03 is undertaken, this rides along for free.
- **Estimated saving:** **5,696 bytes** of heap. Byte count measured and certain; whether the feature is expendable is a project call — hence Low confidence on it actually being taken.
- **Risk:** None to scratch/TLS/sprite. Loses crash forensics; given `CONFIG_ESP_TASK_WDT_PANIC=y` and a 5 s watchdog, that may matter.
- **Verification:** `.dram0.data` drops ~3,440 and `.dram0.bss` ~2,256 in `size -A`; free heap at end of `setup()` rises by the same.

### MEM-09 — `kScratchBytes` has 320 bytes of slack, and its documentation is wrong

- **Location:** `include/platform/png_decode.h:36-44`; the `static_assert` at `src/platform/png_decode.cpp:53`
- **Current cost:** **measured** — `sizeof(platform_png::Work)` = **35,520 bytes** (DWARF `DW_AT_byte_size`), against `kScratchBytes` = 32768 + 2048 + 1024 = 35,840. Slack = **320 bytes**.
- **Is 35,840 the tight minimum? No, but it does not matter.** The lender is the frame sprite's 115,200-byte pixel buffer, which exists for the sprite's own sake and is never freed. Reducing `kScratchBytes` to `sizeof(Work)` frees nothing. `Work` itself is near-irreducible — 32,768 is the DEFLATE window RFC 1951 fixes, 1,536 is two 768-byte scanlines a 256 px RGB row requires, and 1,216 is the two canonical Huffman tables (`2 × (int16_t[16] + int16_t[288])` = 2 × 608). The only real shrink available would be narrowing `Huffman::symbol` from `int16_t`, which would save a few hundred bytes of a borrowed buffer — i.e. nothing.
- **Proposal:** No size change. But the header's arithmetic at `:36-44` is wrong in both directions and should be corrected, because the next person will trust it:
  * "2048  two unfiltered scanlines" — actually **1,536** (2 × 768, as the same comment's own parenthetical says).
  * "1024  Huffman decode tables" — actually **1,216**.
  * Real total: 32,768 + 1,536 + 1,216 = **35,520**, i.e. 320 under the constant.
  The genuinely useful observation to record there is the one the comment does not make: **79,360 bytes of the sprite are idle during a decode**, which is what makes MEM-10 possible.
- **Estimated saving:** **0 bytes.** High confidence — this is a measurement, and the honest answer is that there is no opportunity here.
- **Risk:** n/a (documentation only).
- **Verification:** The existing `static_assert` already guards the relationship; `pio test -e native_test_png` and `make test-live` prove the decoder still decodes.

## Things checked that are *not* opportunities

* **`large_airports_data.cpp` in RAM** — it is not. 59,600 bytes entirely in `.flash.rodata` at `0x3C0EA124`/`0x3C0F41D4`; zero bytes in `.dram0.data` or `.dram0.bss`. `const` POD arrays with no relocations, so the linker places them in the flash mmap window and they are read in place. Nothing to reclaim, and no reason to touch `scripts/build_large_airports.py` for memory reasons.
* **Freeing the frame sprite during a download** — not re-proposed. The prior measurement (largest free block 114,676 vs 115,200 needed, held by TCP `TIME_WAIT`) stands, and `ensureFrameSprite()`'s permanent-allocation design plus `png_decode.h`'s borrowed-scratch contract are the correct answer to it.
* **Recursion** — none on any of our paths (verified across the whole disassembly; the only cycles are `__cxa_*`/`_Unwind_*` and lwIP's `netconn_free`).
* **`s_grid` (3,392 bytes)** — the largest single `.bss` item we own, and not reclaimable. It is read by `ui::terrain::drawTerrainBackground()` at the *start* of every frame, so it cannot live in the sprite (which the same frame is overwriting), and it must survive between downloads. Quantising `elev_m` to `int8_t` in 64 m units would halve it, but at the 20 NM preset one screen pixel is ~308 m of ground, so a 32 m elevation error on a 1:100 coastal slope shifts the painted shoreline by ~10 px. Not worth 1,681 bytes.
* **The embedded VLW font** — 10,766 bytes in flash, referenced by pointer (`src/platform/device/font_blob_embedded.cpp`), never copied to RAM. Each `LGFXBase` that loads it does allocate its own glyph tables: `gCount` = 95 (parsed from `data/ui_font.vlw`) × (4+2+1+1+1) = 855 bytes in five heap blocks, and the font is loaded twice — once on `tft`, once on the sprite via `displayFontEnsureLoaded()`. ~1.8 KB of heap, unavoidable without giving up the smooth font on one target. Worth knowing that `VLWfont::drawChar` also does a `heap_alloc`/`heap_free` per glyph, so the render path is not allocation-free.
* **`tft` (528 bytes `.bss`)**, `s_wm` (620), `s_attrs` (768) — all needed for the whole program in the current architecture; `s_attrs` in particular cannot be a local because `WiFiManagerParameter` stores `custom` by pointer (documented at `wifi_setup_device.cpp:81-88`). MEM-07 is the only route to these.
