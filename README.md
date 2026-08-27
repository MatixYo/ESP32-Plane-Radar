# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display (240×240). Shows a circular **ADS-B radar** around your configured location, with **WiFiManager** for first-time setup.

> **Personal fork** of [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) (MIT). Adds: callsign → route (origin → destination) and operating airline via [adsbdb](https://www.adsbdb.com/), a fading breadcrumb trail per aircraft, climb/descent indicator, Italian city exonyms, expanded regional airport/runway data, tag-collision cycling, and the adsb.fi HTTP/1.0 chunked-encoding fix ([#82](https://github.com/MatixYo/ESP32-Plane-Radar/issues/82)). Upstream copyright and licence are unchanged; see [LICENSE](LICENSE).

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~5 s).

## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Short tap** | Cycle range preset (5 → 10 → 15 → 25 km); saved to flash |
| **Hold 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset (same as the long press).

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`PlaneRadar-Setup`**
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, then save

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://plane-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. Change Wi‑Fi, location, units, or runway overlay; save

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname is `plane-radar` → **plane-radar.local** (`kPortalHostname` in `config.h`). Some clients resolve `.local` slowly; use the IP if needed.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center and ADS-B query position (defaults in `config.h` until set) |
| **Display distances in miles** | Ring scale label in **mi** instead of **km** (e.g. `6mi` vs `10km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Radar display

### Grid

- Dark blue background, subdued green rings and crosshairs
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius)
- White center dot

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

### Runways

- Every `large_airport` worldwide, plus `medium_airport` / `small_airport` in `LOCAL_COUNTRIES` (default Italy) for regional detail — e.g. Verona-Boscomantico, Carpi, Legnago near Mantova
- Only airports with a 4-letter ICAO ident **and** runway end-point coordinates are drawable; most minor Italian grass strips (`IT-xxxx` idents) have no runway geometry in OurAirports and cannot be shown
- All open runway strips in range (helipads excluded); teal lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update / widen the embedded list: edit `LOCAL_COUNTRIES` in `scripts/build_large_airports.py`, then `python3 scripts/build_large_airports.py`

### Aircraft

- **Inside the outer ring** — red heading triangle, yellow breadcrumb trail of the last `kTrackHistoryDepth` fixes (~3 min; fading toward the oldest, cut off at the ring), callsign / type / altitude tags. The type line also carries the operating airline (`A320 Ryanair`) when the callsign resolves to a scheduled flight — same source as the route line, no extra request
- **Climb / descent** — small triangle on the altitude tag line, just before the altitude value: green pointing up for a climb, amber pointing down for a descent; hidden below ±`kVertRateThresholdFpm` (200 ft/min) so level cruise stays clean. Source: `baro_rate`, else `geom_rate` from adsb.fi
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**
- **Overlapping tags take turns** — when two or more tag blocks would collide they form a cluster and only one is shown at a time, swapping every `kAircraftTagCycleMs` (2 s). Symbols stay visible for all; once the tags no longer overlap every label is shown again. Between ADS-B fetches `radarDisplayAnimTick()` repaints just for the swap.

As range decreases (or aircraft approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

### Route (origin → destination)

A fourth tag line shows `Origin>Destination` (muted green) when the callsign
resolves to a scheduled flight:

- Source: [adsbdb](https://www.adsbdb.com/) — one HTTPS `GET /v0/callsign/<cs>` per
  callsign (ADS-B itself carries no route). Runs from the main loop *after* the
  ADS-B fetch so only one `WiFiClientSecure` is alive at once (two exhaust the
  C3 heap → `SSL - Memory allocation failed`). The same response also yields the
  operating airline name shown on the type line.
- Results are cached in RAM: a route or a firm "no route" (adsbdb answers an
  unknown callsign with **HTTP 404** + a valid body) holds for
  `kRouteNegativeTtlMs`; a soft failure (timeout, TLS OOM) retries after the
  shorter `kRouteRetryTtlMs`. Only `kRouteLookupsPerCycle` **new** callsigns are
  queried per ADS-B poll, so the fetch cycle stays short.
- Endpoint label preference: **Italian exonym** (`Londra`), else the city's own
  name ASCII-folded (`Malaga`, `Katowice`, `Bastia`), else the **IATA** code
  (`AGP`) when adsbdb returns no city.
- The exonym table is `src/data/city_exonyms_data.cpp` (hand-curated). Reseed
  candidates with `python3 scripts/build_city_exonyms.py` (Wikidata + OurAirports)
  and prune before committing.
- General aviation, military and non-scheduled callsigns simply have no route.
- Turn the whole feature off with `kRouteLookupEnabled = false` in `config.h`.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/`
- Fetch radius: `ui::radar::fetchRadiusKm()` — scales with the active preset to roughly the screen edge (so rim dots have data)
- Poll interval: `kAdsbFetchIntervalMs` (5 s) in `config.h`
- Ground aircraft hidden by default (`kAdsbShowGroundAircraft`)

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |
| Route lookup | `kRouteLookupEnabled`, `kRouteApiBase`, `kRouteLookupsPerCycle`, `kRouteCacheSize`, `kRouteNegativeTtlMs`, `kRouteRetryTtlMs` |
| Track trail | `kTrackHistoryDepth`, `kTrackHistoryMax`, `kTrackHistoryTtlMs`, `kTrackHistoryMinStepDeg2` |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

## Project layout

```
include/
  config.h
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    large_airports.h
    city_exonyms.h
  ui/
    radar_theme.h
    radar_range.h
    radar_display.h
    runway_overlay.h
    status_screens.h
  services/
    wifi_setup.h
    radar_location.h
    adsb_client.h
    flight_route.h
    track_history.h
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
scripts/
  build_large_airports.py
  build_city_exonyms.py
src/
  main.cpp
  data/
    large_airports_data.cpp
    city_exonyms_data.cpp
  hardware/
  ui/
  services/
    flight_route.cpp        — callsign → origin/destination (adsbdb, cached)
    track_history.cpp       — per-aircraft breadcrumb ring (keyed by ICAO hex)
```

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO env: **`supermini`**
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini` for the Super Mini

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

## Troubleshooting

### Radar connects but never shows aircraft — serial prints `adsb: JSON parse error: InvalidInput`

(See [#82](https://github.com/MatixYo/ESP32-Plane-Radar/issues/82).)


`opendata.adsb.fi` is served through Cloudflare, which answers the API endpoint with
`Transfer-Encoding: chunked` and no `Content-Length` on **HTTP/1.1**.
`readResponseBodyWithPoll()` in `src/services/adsb_client.cpp` reads the raw socket via
`http.getStreamPtr()`, which does **not** strip the chunked framing, so the payload starts
with a hex chunk-size line and `deserializeJson()` bails out with `InvalidInput`. HTTP 200
is received and the body is non-empty, so there is no other error in the log.

Fix — force HTTP/1.0 (no chunked framing; body delimited by connection close, already
handled by the read loop) in `fetchUpdate()`, right after `http.begin(...)`:

```cpp
http.useHTTP10(true);
```

Alternative: replace the manual `getStreamPtr()` loop with `payload = http.getString();`,
which de-chunks correctly regardless of HTTP version (drops the cooperative `pollNetwork()`
during the read).

### Build fails: `'namespace fonts = lgfx::v1::lgfx::v1::fonts;' conflicts with a previous declaration`

LovyanGFX **≥ 1.2.24** changed the trailing declaration in `lgfx/v1/lgfx_fonts.hpp` from a
namespace **alias** to a real `namespace fonts { using namespace lgfx::v1::fonts; }`, which
collides with the identically-named aliases in `radar_display.cpp`, `status_screens.cpp`
and `runway_overlay.cpp`. `lib_deps` uses `lovyan03/LovyanGFX@^1.2.7`, which now resolves
to a broken version.

Fix — pin the last release that still uses an alias, in `platformio.ini`:

```ini
lovyan03/LovyanGFX@1.2.21
```

(or remove the three `namespace fonts = lgfx::v1::fonts;` aliases and let the font names
resolve through LovyanGFX's own `fonts` namespace).

### ESP32-C3-Zero (or other bare C3 boards)

Runs unchanged with the wiring above. The boot-time warning
`spiAttachMISO(): SPI Does not have default pins on ESP32C3!` is harmless — MISO is unused
(`pin_miso = -1`), the panel is write-only.

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

## Fork & upstream

This repository is a personal fork of
[MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) (MIT).
The upstream `LICENSE` and its `Copyright (c) 2026 MatixYo` are kept unchanged;
fork-specific changes are listed in the note at the top of this file.

### How it was set up

1. Forked `MatixYo/ESP32-Plane-Radar` to `Niko12345678/ESP32-Plane-Radar` on GitHub.
2. All local changes were committed on a branch `nicola/enhanced-radar`
   (base: last upstream commit `69c1078`), then pushed to the fork.
3. Merged into the fork's `main` via pull request **#1**
   (`nicola/enhanced-radar` → `main`, merge commit `75624f6`), so the fork's
   `main` now *is* this version.
4. Local `main` tracks `origin/main` (the fork). The `upstream` remote was
   removed — day-to-day work happens only on this fork.

Current remote:

```
origin  https://github.com/Niko12345678/ESP32-Plane-Radar.git   (fetch + push)
```

### Pulling later changes from the original project

`upstream` is not configured. Add it back only when you want to merge new work
from MatixYo:

```bash
git remote add upstream https://github.com/MatixYo/ESP32-Plane-Radar.git
git fetch upstream
git checkout main
git merge upstream/main        # merge, not rebase — main already has a merge commit
git push origin main
git remote remove upstream     # optional: drop it again afterwards
```
