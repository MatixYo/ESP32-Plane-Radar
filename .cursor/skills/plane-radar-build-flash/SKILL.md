---
name: plane-radar-build-flash
description: Build, flash, merge, and release Plane Radar firmware for ESP32-C3 Super Mini. Use when compiling, uploading, creating merged .bin images, running CI locally, or preparing GitHub releases.
---

# Plane Radar Build and Flash

## Quick reference

| Item | Value |
|------|-------|
| PlatformIO env | `supermini` |
| MCU | ESP32-C3, 4 MB flash |
| Serial | 115200 baud, USB CDC on boot |
| Flash offset | 0x0 (merged image) |

## Build workflow

Copy this checklist and track progress:

```
- [ ] pio run -e supermini
- [ ] (optional) pio run -t merge -e supermini
- [ ] Verify output exists
```

### Compile

```bash
pio run -e supermini
```

Output: `.pio/build/supermini/firmware.bin` (+ bootloader, partitions)

### Flash device (USB)

```bash
pio run -t upload -e supermini
pio device monitor   # 115200 baud
```

If upload fails, enter download mode: hold **BOOT** (GPIO 9), tap **RESET**, release BOOT.

### Merge for web flasher

```bash
pio run -t merge -e supermini
# → .pio/build/supermini/firmware-merged.bin
```

Or via shell script (also copies to `release/`):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh          # rebuild + merge
./scripts/merge-firmware.sh --no-build
```

Merge combines bootloader (0x0), partitions (0x8000), boot_app0 (0xe000), app (0x10000).

## CI parity

Match GitHub Actions locally:

```bash
pio run -e supermini
pio run -t merge -e supermini
```

CI uploads: `firmware-merged.bin`, `firmware.bin`, `bootloader.bin`, `partitions.bin`.

## Release

```bash
git tag v1.0.0
git push origin v1.0.0
```

`release.yml` builds, merges, and attaches `plane-radar-v1.0.0.bin` + SHA256 to GitHub Releases.

## Troubleshooting

| Problem | Check |
|---------|-------|
| Compile error in lib deps | `pio pkg update`, verify `platformio.ini` lib versions |
| Upload timeout | Download mode, USB cable, correct port |
| Missing merge input | Run `pio run -e supermini` before merge |
| Display wrong colors | `kDisplayInvert` / `kDisplayRgbOrder` in `config.h` |
