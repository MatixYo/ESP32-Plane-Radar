#!/usr/bin/env bash
# End a device debug session and leave the board running.
#
# Killing OpenOCD outright leaves the CPU halted wherever the debugger stopped
# it. The board then looks dead and the next esptool upload fails with
# "No serial data received", so resume the target before shutting the server
# down. OpenOCD's telnet console (port 4444) is used because it stays reachable
# after GDB has detached.
set -euo pipefail

PATTERN="openocd.*esp32c3-builtin"

if ! pgrep -f "$PATTERN" >/dev/null 2>&1; then
  exit 0
fi

if command -v nc >/dev/null 2>&1; then
  printf 'reset run\nshutdown\n' | nc -w 3 localhost 4444 >/dev/null 2>&1 || true
  for _ in $(seq 1 10); do
    pgrep -f "$PATTERN" >/dev/null 2>&1 || exit 0
    sleep 0.3
  done
fi

pkill -f "$PATTERN" 2>/dev/null || true
