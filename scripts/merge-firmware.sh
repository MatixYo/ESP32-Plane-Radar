#!/usr/bin/env bash
# Build Plane Radar and produce a single .bin for browser-based flashers (esptool-js, etc.).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV="${PIOENV:-supermini}"
NO_BUILD=0
OUT="${ROOT}/release/plane-radar-merged.bin"

usage() {
  cat <<'EOF'
Usage: scripts/merge-firmware.sh [options]

  --no-build     Skip pio run (merge only; firmware must already be built)
  --env NAME     PlatformIO env (default: supermini)
  -o PATH        Output file (default: release/plane-radar-merged.bin)
  -h, --help     Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-build) NO_BUILD=1; shift ;;
    --env) ENV="$2"; shift 2 ;;
    -o) OUT="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -x "${ROOT}/.venv/bin/pio" ]]; then
  PIO="${ROOT}/.venv/bin/pio"
elif command -v pio >/dev/null 2>&1; then
  PIO=pio
elif [[ -x "${HOME}/.platformio/penv/bin/pio" ]]; then
  PIO="${HOME}/.platformio/penv/bin/pio"
else
  echo "PlatformIO (pio) not found. Run: make setup" >&2
  exit 1
fi

cd "$ROOT"

if [[ "$NO_BUILD" -eq 0 ]]; then
  "$PIO" run -e "$ENV"
fi

"$PIO" run -t merge -e "$ENV"

MERGED="${ROOT}/.pio/build/${ENV}/firmware-merged.bin"
if [[ ! -f "$MERGED" ]]; then
  echo "Expected merged image not found: $MERGED" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUT")"
cp "$MERGED" "$OUT"
echo "Wrote ${OUT}"
echo "Flash at offset 0x0 with chip ESP32-C3, 4MB flash (Web Serial flasher)."
