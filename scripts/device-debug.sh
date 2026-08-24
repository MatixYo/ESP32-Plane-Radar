#!/usr/bin/env bash
# Attach terminal GDB to the ESP32-C3 over its built-in USB JTAG.
#
# This is the plain-terminal counterpart to the "Device › …" launch
# configurations, and backs `make debug-device-test` / `make debug-device-run`.
# Handy when the VS Code debug adapter misbehaves or you want a raw GDB prompt.
#
# `pio debug` is deliberately not used: PlatformIO Core 6.1.x crashes on
# Python 3.13+ (asyncio pipe transport), which is what `make setup` installs.
#
# The server side is device-openocd.sh, which flashes over JTAG rather than with
# esptool and resumes the target when it shuts down. Both matter: see the notes
# in that script for why either one wedges the debug module otherwise.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PIOENV_DEBUG="supermini_debug"
BREAK_AT=""
SERVER_ARGS=()

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
    --no-flash) SERVER_ARGS+=(--no-flash); shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

PIO_CORE="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
GDB="${PIO_CORE}/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-gdb"
ELF="${ROOT}/.pio/build/${PIOENV_DEBUG}/firmware.elf"

if [[ ! -x "$GDB" ]]; then
  echo "Missing debug tool: $GDB" >&2
  echo "Run once to let PlatformIO install it:  pio pkg install -e ${PIOENV_DEBUG}" >&2
  exit 1
fi

SERVER_LOG="$(mktemp -t plane-radar-debug)"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    # Terminating the server script runs its own trap, which resumes the target
    # before OpenOCD exits.
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  "${ROOT}/scripts/device-stop.sh" >/dev/null 2>&1 || true
  rm -f "$SERVER_LOG"
}
trap cleanup EXIT INT TERM

# Not a pipeline: $! has to be the server's own PID so that cleanup can signal
# it and let its trap resume the target. The log stays hidden unless something
# goes wrong, otherwise OpenOCD chatter would interleave with the GDB prompt.
echo "==> Building, flashing over JTAG, and starting OpenOCD"
# The +"${...}" guard keeps set -u happy on an empty array: macOS ships bash 3.2,
# where a plain "${arr[@]}" expansion of an empty array is an unbound variable.
"${ROOT}/scripts/device-openocd.sh" ${SERVER_ARGS[@]+"${SERVER_ARGS[@]}"} >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

# device-openocd.sh prints nothing until it knows the board is usable, so this
# line appearing means programming finished and the target was examined.
for _ in $(seq 1 600); do
  grep -qa "Listening on port 3333 for gdb connections" "$SERVER_LOG" && break
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "OpenOCD did not come up:" >&2
    cat "$SERVER_LOG" >&2
    exit 1
  fi
  sleep 0.5
done

if ! grep -qa "Listening on port 3333 for gdb connections" "$SERVER_LOG"; then
  echo "Timed out waiting for OpenOCD:" >&2
  cat "$SERVER_LOG" >&2
  exit 1
fi

if [[ ! -f "$ELF" ]]; then
  echo "Debug firmware not built: $ELF" >&2
  echo "Run: make build-debug" >&2
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
