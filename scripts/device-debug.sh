#!/usr/bin/env bash
# Attach GDB to the ESP32-C3 over its built-in USB JTAG.
#
# `pio debug` is deliberately not used: PlatformIO Core 6.1.x crashes on
# Python 3.13+ (asyncio pipe transport), which is what `make setup` installs.
# OpenOCD and the RISC-V GDB are driven directly instead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PIOENV_DEBUG="supermini_debug"
BREAK_AT=""
DO_FLASH=1

usage() {
  cat <<'EOF'
Usage: scripts/device-debug.sh [options]

  --break SYMBOL   Halt at SYMBOL after reset (e.g. --break setup)
  --no-flash       Attach without rebuilding/reflashing
  -h, --help       Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --break) BREAK_AT="$2"; shift 2 ;;
    --no-flash) DO_FLASH=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

PIO_CORE="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
OPENOCD="${PIO_CORE}/packages/tool-openocd-esp32/bin/openocd"
OPENOCD_SCRIPTS="${PIO_CORE}/packages/tool-openocd-esp32/share/openocd/scripts"
GDB="${PIO_CORE}/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-gdb"
ELF="${ROOT}/.pio/build/${PIOENV_DEBUG}/firmware.elf"

if [[ -x "${ROOT}/.venv/bin/pio" ]]; then
  PIO="${ROOT}/.venv/bin/pio"
elif command -v pio >/dev/null 2>&1; then
  PIO=pio
else
  echo "PlatformIO (pio) not found. Run: make setup" >&2
  exit 1
fi

if [[ "$DO_FLASH" -eq 1 ]]; then
  "$PIO" run -e "$PIOENV_DEBUG" -t upload
fi

for tool in "$OPENOCD" "$GDB"; do
  if [[ ! -x "$tool" ]]; then
    echo "Missing debug tool: $tool" >&2
    echo "Run once to let PlatformIO install it:  pio pkg install -e ${PIOENV_DEBUG}" >&2
    exit 1
  fi
done

if [[ ! -f "$ELF" ]]; then
  echo "Debug firmware not built: $ELF" >&2
  echo "Run: make build-debug" >&2
  exit 1
fi

OPENOCD_LOG="$(mktemp -t plane-radar-openocd)"
OPENOCD_PID=""

cleanup() {
  if [[ -n "$OPENOCD_PID" ]] && kill -0 "$OPENOCD_PID" 2>/dev/null; then
    kill "$OPENOCD_PID" 2>/dev/null || true
    wait "$OPENOCD_PID" 2>/dev/null || true
  fi
  rm -f "$OPENOCD_LOG"
}
trap cleanup EXIT INT TERM

echo "==> Starting OpenOCD (ESP32-C3 built-in USB JTAG)"
"$OPENOCD" -s "$OPENOCD_SCRIPTS" -f board/esp32c3-builtin.cfg >"$OPENOCD_LOG" 2>&1 &
OPENOCD_PID=$!

for _ in $(seq 1 60); do
  if grep -q "Listening on port 3333 for gdb connections" "$OPENOCD_LOG"; then
    break
  fi
  if ! kill -0 "$OPENOCD_PID" 2>/dev/null; then
    echo "OpenOCD exited before the GDB port was ready:" >&2
    cat "$OPENOCD_LOG" >&2
    exit 1
  fi
  sleep 0.5
done

if ! grep -q "Listening on port 3333 for gdb connections" "$OPENOCD_LOG"; then
  echo "Timed out waiting for OpenOCD. Is another debug session running?" >&2
  cat "$OPENOCD_LOG" >&2
  exit 1
fi

GDB_ARGS=(-q "$ELF"
  -ex "target extended-remote localhost:3333"
  -ex "monitor reset halt")

if [[ -n "$BREAK_AT" ]]; then
  GDB_ARGS+=(-ex "thb ${BREAK_AT}" -ex "continue")
  echo "==> GDB will halt at ${BREAK_AT}()"
else
  GDB_ARGS+=(-ex "continue")
  echo "==> Board is running; press Ctrl+C in GDB to break in"
fi

"$GDB" "${GDB_ARGS[@]}"
