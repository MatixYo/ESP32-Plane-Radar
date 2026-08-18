#!/usr/bin/env bash
# Build a compile_commands.json covering both destinations.
#
# PlatformIO emits one database per environment and always writes it to
# $PROJECT_DIR/compile_commands.json, so each run clobbers the previous one.
# A device-only database leaves every file under src/platform/native/ with no
# compile command at all, and clangd then falls back to unrelated flags —
# reporting Panel_sdl as missing in code that compiles perfectly well.
#
# Device entries win for files both environments build, so shared code is
# analysed as it ships. Native-only files contribute their own entries.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PIOENV_DEVICE="${PIOENV:-supermini}"
OUT="${ROOT}/compile_commands.json"

if [[ -x "${ROOT}/.venv/bin/pio" ]]; then
  PIO="${ROOT}/.venv/bin/pio"
elif command -v pio >/dev/null 2>&1; then
  PIO=pio
else
  echo "PlatformIO (pio) not found. Run: make setup" >&2
  exit 1
fi

TMPDIR_DB="$(mktemp -d -t plane-radar-compiledb)"
trap 'rm -rf "$TMPDIR_DB"' EXIT

echo "==> compiledb: ${PIOENV_DEVICE}"
"$PIO" run -t compiledb -e "$PIOENV_DEVICE" >/dev/null
cp "$OUT" "${TMPDIR_DB}/device.json"

NATIVE_DB=""
if "$PIO" run -t compiledb -e native >/dev/null 2>&1; then
  cp "$OUT" "${TMPDIR_DB}/native.json"
  NATIVE_DB="${TMPDIR_DB}/native.json"
  echo "==> compiledb: native"
else
  echo "==> compiledb: native skipped (needs SDL2: brew install sdl2)" >&2
fi

DEVICE_DB="${TMPDIR_DB}/device.json" NATIVE_DB="$NATIVE_DB" OUT="$OUT" python3 - <<'PY'
import json, os

device = json.load(open(os.environ["DEVICE_DB"]))
merged = list(device)
seen = {e["file"] for e in device}

native_path = os.environ.get("NATIVE_DB") or ""
added = 0
if native_path:
    for entry in json.load(open(native_path)):
        if entry["file"] not in seen:
            merged.append(entry)
            seen.add(entry["file"])
            added += 1

with open(os.environ["OUT"], "w") as f:
    json.dump(merged, f, indent=2)

print(f"Wrote compile_commands.json ({len(device)} device + {added} native entries)")
PY
