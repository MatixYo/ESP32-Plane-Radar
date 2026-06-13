#!/usr/bin/env bash
# Flash Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (720×720 MIPI DSI).
# Usage: ./scripts/flash-esp32-p4-waveshare-4c.sh [PORT]
set -euo pipefail
cd "$(dirname "$0")/.."

ENV=esp32-p4-waveshare-4c
PIO="pio"
if [[ -x .venv-pio/bin/pio ]]; then
  PIO=".venv-pio/bin/pio"
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No serial port found. Connect USB TO UART Type-C and retry." >&2
  exit 1
fi

echo "Port: $PORT"
echo "Env:  $ENV (pioarduino / ESP32-P4 + LovyanGFX DSI)"
echo "PIO:  $PIO"

"$PIO" run -e "$ENV" -t upload --upload-port "$PORT"
echo "Done. Monitor: $PIO device monitor -p $PORT -b 115200"
