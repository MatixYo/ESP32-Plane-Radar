# ESP32-Plane-Radar — board port notes

Upstream targets **ESP32-C3 Super Mini + wired GC9A01** (`env:supermini`).

## ESP32-TFT round (HackerBox #0107 / 88267)

Parts-catalog slug: `ESP32_Round_240x240_IPS_Module`

**Kit:** [HackerBox #0107 — Dock](https://www.instructables.com/HackerBox-0107-Dock/) (round ESP32 + GC9A01 module, USB dock, RTL8812BU Wi‑Fi stick).

| | |
|---|---|
| MCU | ESP32-D0WD-V3 |
| Flash | 16 MB |
| USB | WCH CH343 (`1A86:55D3`) |
| Display | GC9A01 240×240 round (integrated SPI) |
| PlatformIO env | **`esp32-tft-round`** |

Pin map: `include/board/esp32_tft_round.h` (hardware only — no credentials).

### Local dev credentials (optional)

```bash
cp include/secrets.h.example include/secrets.h
# Edit secrets.h — gitignored; never commit
```

See [FORK_AND_UPSTREAM.md](FORK_AND_UPSTREAM.md).

### Power before USB (HackerBox module)

1. Plug USB — green LED only, display off.
2. Press the **middle button** — red LED on.
3. CH343 enumerates only when powered on.

Side keys: GPIO **34** (range tap), GPIO **35** (unused in firmware — floats).

### macOS CH343 driver (Apple Silicon)

Use WCH **CH34xVCPDriver.dmg** + Driver Extension → port `/dev/cu.wchusbserial*`. Apple `usbmodem` often fails esptool writes.

Display pins (Setup200 integrated PCB): MOSI **15**, SCLK **14**, CS **5**, DC **27**, RST **33**, BL **22**, invert **true**, rgb_order **false**.

### Build & flash

```bash
pio run -e esp32-tft-round -t upload --upload-port /dev/cu.wchusbserialXXXX
./scripts/flash-esp32-tft.sh /dev/cu.wchusbserialXXXX
pio device monitor -b 115200
```

Upload uses `--no-stub` (required for CH343). If upload fails, hold GPIO **34** or BOOT0 through flash.

### Controls (esp32-tft-round)

- **Short tap GPIO 34** — cycle range (5 → 10 → 15 → 25 km), polled + debounced
- **GPIO 35** — not used (floating pin caused ghost events)
- **Wi-Fi setup** — captive portal unless `secrets.h` sets `kWifiSkipPortal`

### Merged binary

```bash
pio run -e esp32-tft-round -t merge
# .pio/build/esp32-tft-round/firmware-merged.bin @ 0x0
```
