# ESP32-Plane-Radar — Waveshare ESP32-P4 4C port

Target: [Waveshare ESP32-P4-WIFI6-Touch-LCD-4C](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4C) — 720×720 round MIPI DSI, GT911 touch, ESP32-C6 Wi-Fi 6 coprocessor.

| | |
|---|---|
| MCU | ESP32-P4NRW32 |
| Display | JD9365, 720×720, MIPI DSI 2-lane |
| Touch | GT911 / GT9271 (I2C SDA **GPIO7**, SCL **GPIO8**) |
| Wi-Fi | ESP32-C6 via esp_hosted (pre-flashed on board) |
| PlatformIO env | **`esp32-p4-waveshare-4c`** |
| Pin map | `include/board/esp32_p4_waveshare_4c.h` |

## Prerequisites

- **Python 3.10+** for pioarduino (macOS system Python 3.9 is too old). Use a venv:

```bash
python3.12 -m venv .venv-pio
.venv-pio/bin/pip install platformio
.venv-pio/bin/pio run -e esp32-p4-waveshare-4c
```

- **pioarduino** platform (configured in `platformio.ini`; first build downloads the SDK)
- Arduino-esp32 **3.2+** with ESP32-P4 support (via pioarduino)
- LovyanGFX **develop** branch (MIPI DSI / `Bus_DSI`)

## Local Wi-Fi (required for this port)

Hardcoded STA credentials via gitignored `include/secrets.h`:

```bash
cp include/secrets.h.example include/secrets.h
# kWifiSkipPortal = true, set SSID/password
```

Never commit `secrets.h`.

## Build & flash

```bash
pio run -e esp32-p4-waveshare-4c -t upload
pio device monitor -b 115200
```

Or:

```bash
chmod +x scripts/flash-esp32-p4-waveshare-4c.sh
./scripts/flash-esp32-p4-waveshare-4c.sh
```

Use the **USB TO UART** Type-C port for flashing. Hold **BOOT** while resetting if upload fails.

## Controls

| Input | Effect |
|-------|--------|
| **Pinch out** (two fingers spread) | Larger range (5 → 10 → 15 → 25 km) |
| **Pinch in** (two fingers close) | Smaller range |
| **BOOT (GPIO35) short tap** | Cycle range (fallback) |
| **BOOT long press** | Disabled (hardcoded Wi-Fi) |

## Display init

JD9365 vendor init sequence from [xiaozhi-esp32 Waveshare P4 board](https://github.com/78/xiaozhi-esp32/tree/main/boards/waveshare/esp32-p4-wifi6-touch-lcd), converted to LovyanGFX `Panel_DSI` lists:

```bash
python3 scripts/convert_jd9365_init.py   # regenerates include/hardware/panel_jd9365_waveshare_4c.hpp
```

## Merged binary

```bash
pio run -e esp32-p4-waveshare-4c -t merge
# .pio/build/esp32-p4-waveshare-4c/firmware-merged.bin @ 0x0
```

## Troubleshooting

| Symptom | Check |
|---------|--------|
| Blank display | Backlight GPIO26 PWM; MIPI LDO channel 3 @ 2.5 V |
| Wi-Fi never connects | C6 coprocessor powered; serial for esp_hosted errors |
| No pinch response | Serial for `touch: GT911 @ 0x..`; I2C 7/8 |
| Sprite alloc failed | 32 MB PSRAM; `BOARD_HAS_PSRAM` in build flags |

See also [FORK_AND_UPSTREAM.md](FORK_AND_UPSTREAM.md).
