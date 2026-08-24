#!/usr/bin/env bash
# Build the debug firmware, flash it over JTAG, and serve GDB on port 3333.
#
# This is the preLaunchTask behind the "Device › …" launch configurations.
#
# Flashing goes through OpenOCD rather than esptool on purpose. esptool has to
# drive the chip into ROM download mode over USB, and on the ESP32-C3 that
# leaves the RISC-V debug module unable to complete an abstract command
# afterwards — OpenOCD reports "Failed to read MISA from hart 0" and rejects
# GDB. Nothing short of a power cycle brings it back: neither a USB port reset
# nor esptool's own hard reset clears it, because both leave the chip powered.
# Programming over JTAG never enters download mode, so the debug module stays
# usable and a debug session needs no cable pulling.
#
# Note that `make upload` and `make flash-release` still use esptool, which is
# fine for plain flashing but does wedge JTAG. Power-cycle the board once after
# a release flash before starting a debug session.
#
# OpenOCD also starts its GDB server even when it failed to examine the chip,
# so the listening line alone is not a readiness signal — GDB would connect,
# get rejected with "Target not examined yet", and report the unhelpful
# "Connection reset by peer". And a wedged USB JTAG endpoint puts OpenOCD in a
# libusb retry loop that writes megabytes per second of identical errors and
# ignores SIGTERM. So run it under a supervisor: hold its output back until the
# outcome is known, and kill it on the spot if the chip never came up.
#
# Three lines in a healthy session read like failures. "Failed to get flash maps"
# comes before programming, when no valid app image is running to expose its
# mapping table, and OpenOCD falls back to auto-detecting the 4 MB bank. "No
# symbols for FreeRTOS!" is logged on the first thread query, just before the
# qSymbol exchange with GDB completes — GDB does have symbols, it is started with
# firmware.elf. "Too large number of threads" is the task counter read while the
# chip sits halted at the reset vector (see "reset halt" below):
# uxCurrentNumberOfTasks lives in .bss, which the startup code has not zeroed
# yet, so OpenOCD sees garbage from the previous run. All three clear once the
# target continues. Genuine failures are the FATAL_RE patterns below.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PIOENV_DEBUG="supermini_debug"
DO_FLASH=1

usage() {
  cat <<'EOF'
Usage: scripts/device-openocd.sh [options]

  --no-flash   Start OpenOCD against whatever is already on the board
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
BOOT_APP0="${PIO_CORE}/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
BUILD_DIR="${ROOT}/.pio/build/${PIOENV_DEBUG}"

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

# A server left over from an earlier session holds both port 3333 and the USB
# interface, and the run below would fail on both counts.
if pgrep -f "openocd.*esp32c3-builtin" >/dev/null 2>&1; then
  echo "==> Stopping a previous OpenOCD session"
  "${ROOT}/scripts/device-stop.sh"
fi

OPENOCD_ARGS=(-s "$OPENOCD_SCRIPTS" -f board/esp32c3-builtin.cfg)

if [[ "$DO_FLASH" -eq 1 ]]; then
  "$PIO" run -e "$PIOENV_DEBUG"

  for image in "${BUILD_DIR}/bootloader.bin" "${BUILD_DIR}/partitions.bin" \
               "$BOOT_APP0" "${BUILD_DIR}/firmware.bin"; do
    if [[ ! -f "$image" ]]; then
      echo "Missing flash image: $image" >&2
      exit 1
    fi
  done

  # Offsets match what PlatformIO passes to esptool for this board.
  OPENOCD_ARGS+=(
    -c "program_esp \"${BUILD_DIR}/bootloader.bin\" 0x0 verify"
    -c "program_esp \"${BUILD_DIR}/partitions.bin\" 0x8000 verify"
    -c "program_esp \"${BOOT_APP0}\" 0xe000 verify"
    -c "program_esp \"${BUILD_DIR}/firmware.bin\" 0x10000 verify compress"
  )
else
  # program_esp runs init itself, so this is only needed when it is skipped —
  # without it "reset halt" below runs at config stage and OpenOCD bails out
  # with `invalid command name "reset"`.
  OPENOCD_ARGS+=(-c "init")
fi

# Printed last, so it marks the end of programming rather than the moment the
# GDB port opened — OpenOCD opens that port before it starts writing flash.
READY_SENTINEL="PLANE_RADAR_OPENOCD_READY"
OPENOCD_ARGS+=(-c "reset halt" -c "echo ${READY_SENTINEL}")

LOG="$(mktemp -t plane-radar-openocd)"
OPENOCD_PID=""
TAIL_PID=""

cleanup() {
  [[ -n "$TAIL_PID" ]] && kill "$TAIL_PID" 2>/dev/null || true
  if [[ -n "$OPENOCD_PID" ]] && kill -0 "$OPENOCD_PID" 2>/dev/null; then
    # Resume before shutting down. Killing the server while the CPU sits halted
    # leaves the debug module unable to examine the hart on the next run, and
    # only a power cycle clears that.
    #
    # Done inline rather than through device-stop.sh because that script matches
    # OpenOCD by process name: a session exiting late would otherwise tear down
    # a newer one that has already taken its place. Only this server is touched,
    # and while it is alive it is the one holding port 4444.
    if command -v nc >/dev/null 2>&1; then
      printf 'reset run\nshutdown\n' | nc -w 3 localhost 4444 >/dev/null 2>&1 || true
      for _ in $(seq 1 10); do
        kill -0 "$OPENOCD_PID" 2>/dev/null || break
        sleep 0.3
      done
    fi
    # SIGKILL, not SIGTERM: a wedged OpenOCD never returns to its signal handler.
    kill -9 "$OPENOCD_PID" 2>/dev/null || true
  fi
  rm -f "$LOG"
}
# TERM and INT matter as much as EXIT here: VS Code sends SIGTERM when a task is
# stopped, and the target has to be resumed before the server dies.
trap cleanup EXIT INT TERM

echo "==> Flashing over JTAG and starting the GDB server"
"$OPENOCD" "${OPENOCD_ARGS[@]}" >"$LOG" 2>&1 &
OPENOCD_PID=$!

FATAL_RE='examination failed|LIBUSB_ERROR|IN buffer overflow|tap/device found: 0xffffffff|Programming Failed|Unable to reset target|OpenOCD init failed'

status=timeout
deadline=$((SECONDS + 240))
while ((SECONDS < deadline)); do
  # Checked before the ready pattern: OpenOCD prints both, failure line first.
  if grep -qaE "$FATAL_RE" "$LOG"; then status=fatal; break; fi
  if grep -qa "$READY_SENTINEL" "$LOG"; then status=ready; break; fi
  if ! kill -0 "$OPENOCD_PID" 2>/dev/null; then status=exited; break; fi
  sleep 0.2
done

if [[ "$status" != "ready" ]]; then
  # Reaping a SIGKILLed job makes bash announce "Killed: 9" on its own stderr,
  # which buries the explanation below. Mute the shell while it happens.
  exec 3>&2 2>/dev/null
  kill -9 "$OPENOCD_PID" 2>/dev/null || true
  wait "$OPENOCD_PID" 2>/dev/null || true
  exec 2>&3 3>&-
  OPENOCD_PID=""
  # Drop the retry-loop noise, which can be millions of identical lines.
  grep -avE 'LIBUSB_ERROR' "$LOG" | head -n 40
  cat >&2 <<EOF

==> OpenOCD could not take control of the ESP32-C3 debug module ($status).

If the log above shows "Failed to read MISA from hart 0", the debug module is
wedged. That happens when the chip was last flashed with esptool (\`make upload\`
or \`make flash-release\`), which drives it through ROM download mode. Only
removing power clears it:

  1. Unplug the board from USB
  2. Wait ~5 seconds and plug it back in
  3. Start the debug session again

EOF
  exit 1
fi

# Held back until now so that a failed start cannot flood the terminal, and so
# the VS Code problemMatcher sees the "Listening on port 3333" line only once
# the board is genuinely ready for GDB.
cat "$LOG"
tail -f -n "+$(($(wc -l <"$LOG") + 1))" "$LOG" &
TAIL_PID=$!

wait "$OPENOCD_PID" || true
OPENOCD_PID=""
