# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for an **ESP32-C3 Super Mini** and a round SPI display — **1.28″ GC9A01** (240×240) or **2.1″ GC9B72** (360×360). Shows a circular **ADS-B radar** around your configured location, with **WiFiManager** for first-time setup.

The radar layout is derived from the panel size at runtime, so both displays get the same design scaled to fit rather than a second set of hand-tuned constants.

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
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
scripts/
  build_large_airports.py
src/
  main.cpp
  data/
    large_airports_data.cpp
  hardware/
  ui/
  services/
```

## Wiring

`BOOT (user)` is the on-board button on **GPIO 9** in every combination below — no wire.

### GC9A01 (1.28″ round, 240×240) ↔ ESP32-C3 Super Mini

The shipping build.

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

### GC9B72 (2.1″ round, 360×360) — proposed

> **In progress, not yet supported by the firmware.** The ESP32-C6 column also
> needs a PlatformIO platform shipping Arduino core 3.x — the official
> `espressif32` is still on 2.0.17 and has no C6 Arduino variant.

| Display | ESP32-C3 | ESP32-C6 | |
|---------|----------|----------|---|
| VCC | 3V3 | 3V3 | **3.3 V only** — the module has no regulator |
| GND | GND | GND | |
| RES | GPIO **0** | GPIO **0** | |
| CS | GPIO **1** | GPIO **1** | |
| DC | GPIO **10** | GPIO **2** | the one wire that differs — GPIO 10 is not broken out on the C6 |
| SDA (MOSI) | GPIO **3** | GPIO **3** | |
| SCL (SCLK) | GPIO **4** | GPIO **4** | |
| BL | GPIO **5** | GPIO **5** | backlight, on a GPIO so it can be dimmed |
| SDO (MISO) | GPIO **6** | GPIO **6** | optional — see below |
| TE | GPIO **7** | GPIO **7** | optional — see below |
| BOOT (user) | GPIO **9** | GPIO **9** | on-board button |

**SDO is unused by the radar.** Frames are composed in an off-screen sprite and
pushed in one pass, so nothing reads back from the panel and `pin_miso` stays
`-1`. It matters only on the fallback path taken when the sprite cannot be
allocated, where LovyanGFX's antialiased primitives read the panel in order to
blend against it — with SDO floating, those blends read garbage.

**TE is not used by LovyanGFX.** Both GC9 init lists enable the tearing-effect
output (`0x35 0x00`), but no SPI panel driver in LovyanGFX consumes it
(`getScanLine()` returns `-1`), so using it means polling the pin from
application code and starting the push just after the pulse. A full 360×360
frame is 259,200 bytes on the wire — about 52 ms at 40 MHz — long enough for a
seam to be visible.

**Strapping pins.** Do not move DC to GPIO 2 on the **C3** to make the two boards
identical: there it is a boot strapping pin that must be high at reset, and a
high-impedance DC input will not hold it. On the C6, GPIO 2 is not a strapping
pin (the C6's are GPIO 4, 5, 8, 9 and 15).

**Board orientation.** Holding the C3 Super Mini with USB-C up, `GPIO5` and `5V`
are the pads nearest the connector and `GPIO20`/`GPIO21` the two furthest.
Published pinouts appear in both orientations — check the silkscreen, not a
diagram.

**Backlight current.** The 2.1″ backlight draws several times what the 1.28″ one
does, and rides the Super Mini's 3V3 LDO alongside WiFi TX bursts. Measure 3V3
under load before trusting it; the TX power is already capped to 8.5 dBm for
related reasons.

## Build

Pick the environment that matches the display fitted:

| Display | Environment | Resolution |
|---------|-------------|------------|
| 1.28″ GC9A01 | `supermini` | 240×240 |
| 2.1″ GC9B72 | `supermini_gc9b72` | 360×360 |

```bash
# 1.28" GC9A01 (default)
pio run -e supermini -t upload

# 2.1" GC9B72
pio run -e supermini_gc9b72 -t upload

pio device monitor
```

- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini` for the Super Mini
- The two environments differ only by `-DPLANE_RADAR_PANEL_GC9B72`, which selects
  the panel driver, pins, SPI clock and colour settings in `include/config.h`
- The 360×360 frame buffer does not fit at 16 bits on an ESP32-C3, so the radar
  falls back to 8-bit RGB332 (129,600 B instead of 259,200 B). The chosen depth is
  printed at boot:

  ```
  radar: frame sprite 360x360 @8bpp
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
