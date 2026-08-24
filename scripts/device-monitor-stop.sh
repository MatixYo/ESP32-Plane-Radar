#!/usr/bin/env bash
# Close the serial monitor so the USB CDC port is free for the next upload.
#
# esptool needs exclusive access to /dev/cu.usbmodem*. With a monitor still
# attached it never completes the handshake and fails with "device reports
# readiness to read but returned no data (device disconnected or multiple access
# on port?)" — which reads like a cable fault, not a busy port.
#
# This runs as the post-debug step of "Device › Attach (debugger)", which starts
# a monitor of its own. The task terminal stays open afterwards, so the log that
# was already printed remains scrollable.
#
# The pattern lives in a script rather than inline in tasks.json on purpose:
# `pkill -f` matches full command lines, and a shell invoked with the pattern as
# part of its own argv is a match for it — the task would kill itself before
# reaching the monitor.
set -euo pipefail

PATTERN="pio device monitor"

if ! pgrep -f "$PATTERN" >/dev/null 2>&1; then
  exit 0
fi

pkill -f "$PATTERN" 2>/dev/null || true

for _ in $(seq 1 10); do
  pgrep -f "$PATTERN" >/dev/null 2>&1 || exit 0
  sleep 0.3
done

pkill -9 -f "$PATTERN" 2>/dev/null || true
