#!/usr/bin/env bash
# Flash the debug firmware, then run OpenOCD in the foreground as a GDB server.
#
# This is the preLaunchTask behind the "Device › …" launch configurations. The
# VS Code task watches stdout for OpenOCD's "Listening on port 3333" line and
# only then lets the debug adapter attach.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PIOENV_DEBUG="supermini_debug"
DO_FLASH=1

usage() {
  cat <<'EOF'
Usage: scripts/device-openocd.sh [options]

  --no-flash   Start OpenOCD without rebuilding/reflashing first
  -h, --help   Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-flash) DO_FLASH=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

PIO_CORE="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
OPENOCD="${PIO_CORE}/packages/tool-openocd-esp32/bin/openocd"
OPENOCD_SCRIPTS="${PIO_CORE}/packages/tool-openocd-esp32/share/openocd/scripts"

if [[ -x "${ROOT}/.venv/bin/pio" ]]; then
  PIO="${ROOT}/.venv/bin/pio"
elif command -v pio >/dev/null 2>&1; then
  PIO=pio
else
  echo "PlatformIO (pio) not found. Run: make setup" >&2
  exit 1
fi

if [[ ! -x "$OPENOCD" ]]; then
  echo "OpenOCD not found: $OPENOCD" >&2
  echo "Run once to let PlatformIO install it:  pio pkg install -e ${PIOENV_DEBUG}" >&2
  exit 1
fi

# Must happen before the upload: a server left over from an earlier session holds
# both port 3333 and the USB interface, and esptool then fails with
# "No serial data received" partway through flashing.
if pgrep -f "openocd.*esp32c3-builtin" >/dev/null 2>&1; then
  echo "==> Stopping a previous OpenOCD session"
  "${ROOT}/scripts/device-stop.sh"
fi

if [[ "$DO_FLASH" -eq 1 ]]; then
  "$PIO" run -e "$PIOENV_DEBUG" -t upload
fi

exec "$OPENOCD" -s "$OPENOCD_SCRIPTS" -f board/esp32c3-builtin.cfg
