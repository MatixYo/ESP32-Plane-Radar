#!/usr/bin/env bash
# Flash ESP32-TFT round module (CH343 USB, 16 MB merged image).
# Usage: ./scripts/flash-esp32-tft.sh [PORT]
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.wchusbserial* /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No serial port found. Power on module (middle button) and replug USB." >&2
  exit 1
fi

echo "Port: $PORT"
echo "HackerBox 0107: press the MIDDLE power button first (red LED) so CH343 enumerates."
echo "macOS Apple Silicon: WCH CH34xVCPDriver.dmg + Driver Extension, then /dev/cu.wchusbserial*"

if pio run -e esp32-tft-round -t upload --upload-port "$PORT"; then
  echo "Done. Monitor: pio device monitor -p $PORT -b 115200"
  exit 0
fi

echo "PlatformIO upload failed; trying merged binary at 0x0..." >&2
pio run -e esp32-tft-round -t merge
MERGED=".pio/build/esp32-tft-round/firmware-merged.bin"
ESPTOOL_DIR="$(pio pkg exec -p espressif32 --where tool-esptoolpy 2>/dev/null | head -1)"
if [[ -z "$ESPTOOL_DIR" || ! -f "$ESPTOOL_DIR/esptool.py" ]]; then
  echo "Could not locate esptool.py from PlatformIO espressif32 package." >&2
  exit 1
fi
python3 "$ESPTOOL_DIR/esptool.py" \
  --chip esp32 --port "$PORT" --baud 115200 \
  --before default_reset --after hard_reset \
  --no-stub write_flash --flash_mode dio --flash_freq 40m --flash_size 16MB \
  0x0 "$MERGED"
echo "Done. Monitor: pio device monitor -p $PORT -b 115200"
