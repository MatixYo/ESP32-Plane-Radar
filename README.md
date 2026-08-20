# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display (240×240). Shows a circular **ADS-B radar** around your configured location, with **WiFiManager** for first-time setup.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid, over an optional green terrain relief from [AWS Open Data terrain tiles](https://registry.opendata.aws/terrain-tiles/)

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~3 s).

## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Short tap** | Cycle range preset (10 → 20 → 40 → 80 NM); saved to flash (~500 ms after release, so a double tap does not also change range) |
| **Double tap** | Cycle configured airport sites (ICAO list from the portal); active code shown on the left of the radar |
| **Hold 3 s** | Clear Wi‑Fi, location, airport list, units, and overlay toggles (runways and terrain back to on); reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset (same as the long press).

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`PlaneRadar-Setup`**
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, then save

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://plane-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. Change Wi‑Fi, location, units, runway overlay, or terrain layer; save

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname is `plane-radar` → **plane-radar.local** (`kPortalHostname` in `config.h`). Some clients resolve `.local` slowly; use the IP if needed.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Airport 1–6 (ICAO)** | Up to six large airports to cycle with a double tap (e.g. `LOWG`, `LOWW`); leave unused slots blank. Blanking all six restores the default airport (`kDefaultSiteIdent`) |
| **Display distances in km** | Ring scale label in **km** instead of the default **NM** (e.g. `74km` vs `40NM`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |
| **Show terrain** | Green elevation shading under the radar grid (default: on; off keeps the plain background) |

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Native dev harness

The same firmware sources also build as a **native macOS binary**, so radar layout and logic
can be iterated without flashing a board:

```bash
make native        # build and run (720x720 window, 240x240 logical at 3x)
make native-build  # compile only
make test          # host unit tests
```

This is **not an emulator** — there is no virtual CPU and no virtual SPI bus. It is your own
C++ compiled by clang, with LovyanGFX's `Panel_sdl` backend writing pixels into an SDL window
instead of `Panel_GC9A01` writing them over SPI. All business logic and all drawing code are
shared with the firmware.

Requires `brew install sdl2` (libcurl ships with macOS).

Homebrew's `sdl2` is now **sdl2-compat**, which serves the SDL2 API from SDL3, and SDL3 enforces AppKit's rule that windows may only be touched from the main thread. `Panel_sdl::main()` owns that thread and runs `setup()`/`loop()` on a worker, so **any SDL window call made from shared code aborts the harness** with `NSWindow geometry should only be modified on the main thread!`. Configure the window before `tft.init()` — as `displayInit()` does for the title — so the setting is applied during `SDL_CreateWindow` on the main thread.

| | Device | Native |
|---|---|---|
| Settings | NVS | `~/.plane-radar/settings.json` (`$PLANE_RADAR_SETTINGS` overrides) |
| Config portal | captive portal on the AP | `http://127.0.0.1:8080` |
| BOOT button | GPIO 9 | **SPACE** key (single/double tap and 3 s hold) |
| Aircraft data | live adsb.fi | live adsb.fi |

Extra shortcuts from LovyanGFX: **Ctrl+1…6** rescales the window, **Ctrl+L/R** rotates it.

### Fidelity

The radar's layout is derived at runtime from VLW font metrics, so the harness is only useful
if those metrics match the device exactly. `docs/fidelity-baseline.txt` records them for both
destinations; regenerate with `PLATFORMIO_BUILD_FLAGS="-DPLANE_RADAR_FRAME_HASH" pio run -e native -t exec`.

Everything composited into the `LGFX_Sprite` frame buffer — grid, rings, labels, aircraft,
runway overlay — is pixel-exact across destinations. The three status screens are layout-exact
but blend-approximate: they draw anti-aliased circles straight to the panel, and the device
cannot read back pixels (`pin_miso = -1`), so it blends against assumed black.
**New UI work should draw into the sprite; direct-to-panel anti-aliasing is a defect.**

Note the ESP32-C3 has no FPU, so `sinf`/`cosf`/`atan2f` come from soft-float newlib on device
and macOS libm natively. Trig-derived aircraft positions can differ by 1 px; text metrics and
integer layout are exact.

`~/.plane-radar/settings.json` stores the Wi-Fi SSID in plaintext. The harness is a
development tool, not a product.

## Radar display

### Grid

- Dark blue background (terrain shading paints over it when enabled), subdued green rings and crosshairs
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius)
- White center dot

Layout and colors: `include/ui/radar_theme.h`.

### Terrain

- Green-shaded elevation background under the grid: hypsometric bands from darker lowlands to lighter highlands; water and sea level keep the plain background
- Elevation from the [AWS Open Data terrain tiles](https://registry.opendata.aws/terrain-tiles/) bucket (`terrarium` PNGs — no key, no rate limit, no sign-up): each 256×256 tile carries 65 536 bare-earth samples, encoded per pixel as `R*256 + G + B/256 - 32768` metres
- A view needs only the **1–4 tiles** its bounding box touches; the zoom is chosen per view as the highest one whose tiles still cover it in a 2×2 block, so wider ranges get coarser terrain and the 10 NM preset the finest
- Tiles are decoded as they arrive and resampled straight into a **41×41 grid** (3.4 KB) — the raster is never held in RAM
- One tile per main-loop pass, so the radar keeps running while terrain loads; tiles are 60–150 KB, so a full grid arrives within a few seconds
- **One** grid is cached, for the view on screen; changing range or centre re-fetches. A failed tile is retried in place, and a download that keeps failing backs off for 60 s
- Needs Wi‑Fi; until the grid arrives the radar runs normally on the plain background
- Toggle with **Show terrain** in the Wi‑Fi setup portal (default: on)

Tiles come over `https://` — the bucket policy requires it and answers plain HTTP with 403 — so a tile fetch needs a TLS session (~30 KB) and a decoder (~36 KB) while the 115 KB frame sprite is already up. On the ESP32-C3 that is about 13 KB more than the heap has, which makes memory, not bandwidth, this layer's real constraint.

Freeing the sprite for the duration does not solve it: measured, the TLS path leaves a few hundred bytes stranded inside the 115 KB hole (largest free block comes back as 114 676 against the 115 200 needed) and TCP `TIME_WAIT` holds them there, so the sprite never returns and every later frame has to be painted straight onto the panel. So the decoder in `platform/png_decode.cpp` — own inflate, no pngle — **allocates nothing at all**: it works in scratch the display lends it, which is the frame sprite's own pixels. Nobody is compositing a frame while a tile decodes, the panel keeps showing the last one blitted, and the frame is repainted when the grid lands. Nothing is allocated at run time, so nothing can fail to be allocated.

Being our own decoder, it also verifies zlib's Adler-32 over every tile: structural checks alone can pass on a stream that inflates to the wrong bytes, and the failure mode that hides behind is terrain that looks perfectly plausible. A tile that fails it is refused and retried like any other bad tile.

Upsampling the grid to the frame is fixed-point for the same reason of scale: it touches 57 600 pixels per redraw, and the C3 core has no FPU, so in floats it measured 209 ms of a 297 ms frame against 37 ms in integers.

Tiles replaced per-point elevation queries, which billed hundreds of coordinates per view: 7 rate-limited requests paced 12 s apart, about 1.5 min per view, and HTTP 429 when the budget slipped.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) | ADS-B fetch radius |
|------------|-------------------------------|--------------------|
| 10 NM / 19 km | ~13 NM (25 km) | ~15 NM (27 km) |
| 20 NM / 37 km (default) | ~27 NM (49 km) | ~29 NM (54 km) |
| 40 NM / 74 km | ~53 NM (99 km) | ~59 NM (109 km) |
| 80 NM / 148 km | ~107 NM (198 km) | ~118 NM (218 km) |

Preset and NM/km choice persist across reboot (`planeradar` NVS namespace).

### Runways

- Major airports from OurAirports (`large_airport`); all open runway strips in range (helipads excluded)
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update the embedded list: `python3 scripts/build_large_airports.py`

### Aircraft

- **Inside the outer ring** — red heading triangle, magenta speed vector (clipped at the ring), callsign / type / altitude tags
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**

As range decreases (or aircraft approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/`
- Fetch radius: `ui::radar::fetchRadiusKm()` — scales with the active preset to roughly the screen edge (so rim dots have data)
- Poll interval: `kAdsbFetchIntervalMs` (3 s) in `config.h`
- Ground aircraft hidden by default (`kAdsbShowGroundAircraft`)

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Default location | `kDefaultSiteIdent` (ICAO seeded when the airport list is empty) |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |
| Terrain | `kTerrainGridSize`, `kTerrainTileUrlFmt`, `kTerrainRequestTimeoutMs`, `kTerrainTileIntervalMs`, `kTerrainRetryIntervalMs` |

Range presets: `include/core/settings.h` (`kRangePresets`).

## Project layout

```
include/
  config.h                 — portable settings shared by both destinations
  core/                    — portable logic: no Arduino, no LovyanGFX
    platform.h             — the seam: clock, log, reboot, storage, HTTP, font
    settings.h             — location, range preset, units, runway toggle
    geo.h                  — lat/lon to screen projection
    adsb.h, aircraft.h     — ADS-B fetch and decode
    terrain.h              — terrain-tile elevation grid fetch and cache
    portal_params.h        — config-portal field table (one per destination)
    large_airports.h
  ui/                      — LovyanGFX drawing, shared by both destinations
    display.h, display_font.h, radar_theme.h, radar_range.h
    radar_display.h, runway_overlay.h, terrain_overlay.h, status_screens.h
  platform/
    wifi_setup.h           — radio + BOOT button seam
    png_decode.h           — streaming PNG decoder, decodes into lent scratch
    device/                — pins.h, lgfx_config_device.hpp
    native/                — lgfx_config_native.hpp
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
scripts/
  build_large_airports.py
  gen_png_fixtures.py        — PNG test fixtures for native_test_png
src/
  main.cpp                 — setup()/loop(), shared verbatim
  core/                    — settings, geo, adsb, terrain, portal_params, airport data
  ui/                      — radar_display, runway_overlay, terrain_overlay,
                             status_screens, display_font
  platform/
    device/                — NVS, HTTPClient, WiFiManager, GC9A01, embedded font
    native/                — JSON settings, libcurl, SDL panel, keyboard BOOT,
                             simulated radio, localhost config portal
test/                      — host unit tests (make test); the tile download
                             flow and the PNG decoder need envs of their own,
                             native_test_fetch and native_test_png
```

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **2** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

## Build

First-time setup on a machine:

```bash
make setup
make build
```

### VS Code / Cursor workflow

Everything runs from **Run and Debug (F5)** — pick a configuration from the dropdown:

| Configuration | What it does |
|---------------|--------------|
| **Emulator › Run (debug)** | Native SDL harness; breakpoints in `core/`, `ui/`, `main.cpp` |
| **Emulator › Test (debug)** | One unit test suite; prompts for `test_geo` / `test_settings` |
| **Device › Run (debug)** | Flash debug firmware, attach to the board, let it run |
| **Device › Test (debug)** | Flash debug firmware, attach, halt at `setup()` |
| **Device › Flash release** | Release build, flash to the board (no debugging) |

Both test suites link to the same `.pio/build/native_test/program`, so the suite is picked at launch instead of debugging whichever happened to build last.

The device configurations set clickable IDE breakpoints in the editor, same as the emulator ones. Each starts OpenOCD as a pre-launch step and shuts it down afterwards, so the JTAG interface is not left held.

Extensions ([CodeLLDB](https://open-vsx.org/extension/vadimcn/vscode-lldb) for the emulator, [Native Debug](https://open-vsx.org/extension/webfreak/debug) for the board) are listed in `.vscode/extensions.json` and offered on first open.

**Terminal → Run Task…** covers the non-debug work (**⇧⌘B** runs **Device › Flash release**):

| Task | What it does |
|------|--------------|
| **Device › Flash release + monitor** | Flash release, then serial log |
| **Emulator › Run / Test** | Harness or full test run, no debugger |
| **Emulator › Run with ASan** | Harness under AddressSanitizer/UBSan |
| **Release › Build + merge** | CI-parity release build + merged `.bin` |
| **Device › … (debug, terminal GDB)** | Same device sessions as raw GDB, if the adapter misbehaves |

```bash
make flash-release      # release build + flash
make debug-device-test  # on-device GDB, halt at setup()
make debug-device-run   # on-device GDB, board runs
make native             # emulator run
make test               # all host unit tests
make test-live          # opt-in live terrain tile fetch (needs internet)
```

- PlatformIO envs: **`supermini`** (release), **`supermini_debug`** (`-Og -g`, on-device GDB), **`native`** / **`native_test`** / **`native_test_fetch`** / **`native_test_png`** / **`native_test_live`** (host)
- `native_test_fetch` runs the tile download state machine against a scripted HTTP client, fake clock and fake PNG decoder — no network. `native_test_png` runs the real decoder against generated PNGs (every filter, all three DEFLATE block types, split IDATs, truncated and corrupt streams, a stream that inflates to the wrong bytes with a valid structure, one that runs past the declared height) plus a full-size tile, and checks it writes nothing outside the scratch it was lent; fixtures come from `scripts/gen_png_fixtures.py`. `native_test_live` (`make test-live`) is an opt-in smoke test that pulls a real tile from the AWS bucket, so it stays out of `make test`
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini` for the Super Mini

Flashing uses `upload_flags = --no-stub`. Over the ESP32-C3's built-in USB Serial/JTAG, esptool's flasher stub stops responding right after `Stub running...` and the upload dies with `Unable to verify flash chip connection`. The ROM loader takes about 40 s instead of 15 s, but it does not fail. `upload_speed` is pinned to 115200 for the same reason — the baud renegotiation is another failure point, and the nominal rate is meaningless over USB CDC anyway.

`Failed to read MISA from hart 0` means the RISC-V debug module is wedged. It looks exactly like dead hardware, but the board is fine — and no reset clears it, because every reset available over USB leaves the chip powered. Only unplugging it does. Two things put it in that state, and both are avoided by the debug scripts:

- **Flashing with esptool**, which drives the chip through ROM download mode. This is why the debug flow programs over JTAG instead. `make upload` and `make flash-release` still use esptool, so power-cycle the board once after either before starting a debug session.
- **Ending a session with the CPU halted.** `scripts/device-stop.sh` resumes the target through OpenOCD's telnet console before shutting the server down, and `scripts/device-openocd.sh` calls it on `EXIT`, `INT` and `TERM` — `TERM` included because that is what VS Code sends when a task is stopped.

A leftover OpenOCD also holds port 3333 and the USB interface, so `scripts/device-openocd.sh` clears any previous session before it starts.

#### On-device debugging

Debugging goes over the ESP32-C3's built-in USB JTAG — no extra probe, just the USB-C cable. `scripts/device-openocd.sh` builds the debug firmware, writes all four images over JTAG with OpenOCD's `program_esp`, and then serves GDB on port 3333; the Native Debug adapter attaches `riscv32-esp-elf-gdb` to it.

Readiness is signalled by a sentinel printed once programming has finished, not by OpenOCD's `Listening on port 3333` line — OpenOCD opens that port before it starts writing flash, so attaching on it would drop GDB into the middle of an upload. The script also treats a failed target examination as fatal and stops the server itself, because OpenOCD otherwise serves a target it cannot control and GDB reports only `Connection reset by peer`.

`pio debug` is deliberately unused: PlatformIO Core 6.1.x drives its debug session through an asyncio pipe transport that raises `OSError: [Errno 22]` on Python 3.13+, which is the Python `make setup` installs. GDB would start but never attach to the target.

`scripts/device-debug.sh` runs the same session as plain terminal GDB, which is what `make debug-device-test` / `make debug-device-run` use. Handy when the adapter misbehaves or you want GDB commands directly:

```
break radar_display.cpp:120
continue
bt
info locals
```

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and similar tools (ESP32-C3, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/plane-radar-merged.bin`. Skip rebuild if firmware is already built:

```bash
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/supermini/firmware-merged.bin`):

```bash
pio run -e supermini
pio run -t merge -e supermini
```

Put the board in download mode (hold **BOOT**, tap **RESET**), then flash with Chrome/Edge over USB.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Artifact `plane-radar-supermini` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release asset `plane-radar-v1.0.0.bin` + `.sha256` |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release workflow builds firmware in CI and attaches the merged image to the release. Download from **Releases** on GitHub, then flash at **0x0** (ESP32-C3, 4 MB).

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
