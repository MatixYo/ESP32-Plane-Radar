#!/usr/bin/env bash
# Generate .clangd for ESP32 cross-compilation (fixes "_ansi.h file not found").
#
# clangd parses with host libc++ unless we point it at the PlatformIO
# riscv32-esp-elf newlib headers. Run via `make compiledb` after building.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

find_gxx() {
  if [[ -n "${PLATFORMIO_GXX:-}" && -x "${PLATFORMIO_GXX}" ]]; then
    echo "${PLATFORMIO_GXX}"
    return
  fi
  local candidate
  candidate="$(command -v riscv32-esp-elf-g++ 2>/dev/null || true)"
  if [[ -n "${candidate}" ]]; then
    echo "${candidate}"
    return
  fi
  candidate="${HOME}/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++"
  if [[ -x "${candidate}" ]]; then
    echo "${candidate}"
    return
  fi
  echo "riscv32-esp-elf-g++ not found; run 'make build' first" >&2
  exit 1
}

GXX="$(find_gxx)"
SYS_INCLUDES="$(
  "${GXX}" -E -x c++ - -v </dev/null 2>&1 |
    awk '/#include <...> search starts here:/{found=1; next} /End of search list/{found=0} found {gsub(/^[ \t]+/, ""); print}'
)"

if [[ -z "${SYS_INCLUDES}" ]]; then
  echo "Failed to read system include paths from ${GXX}" >&2
  exit 1
fi

{
  echo "CompileFlags:"
  echo "  CompilationDatabase: ."
  echo "---"
  echo "# The cross-compilation flags below are wrong for the native harness,"
  echo "# which clang builds for the host against SDL2. Those files carry their"
  echo "# own flags in compile_commands.json, so leave them alone."
  echo "# PathExclude is resolved relative to this file's directory."
  echo "If:"
  echo "  PathExclude: src/platform/native/.*"
  echo "CompileFlags:"
  echo "  Add:"
  while IFS= read -r inc; do
    [[ -z "${inc}" ]] && continue
    echo "    - -isystem${inc}"
  done <<< "${SYS_INCLUDES}"
  echo "    - --target=riscv32-esp-elf"
  echo "  Remove:"
  echo "    - -march=*"
  echo "    - -mcpu=*"
  echo "    - -fstrict-volatile-bitfields"
  echo "    - -fno-tree-switch-conversion"
  echo "    - -fno-jump-tables"
  echo "    - -nostartfiles"
} > .clangd

count="$(printf '%s\n' "${SYS_INCLUDES}" | sed '/^$/d' | wc -l | tr -d ' ')"
echo "Wrote .clangd (${count} toolchain include paths from ${GXX})"
