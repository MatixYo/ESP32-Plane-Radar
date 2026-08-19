# Plane Radar — local environment setup and PlatformIO build targets.
#
# First time on a machine:
#   make setup
#   make build
#
# VS Code / Cursor: run tasks from the Command Palette (Terminal: Run Task…).

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
VENV := $(ROOT)/.venv
PIOENV ?= supermini
SYSTEM_PYTHON ?= python3

# Project venv (created by `make setup`) avoids PEP 668 / Homebrew pip restrictions.
export PATH := $(VENV)/bin:$(HOME)/.platformio/penv/bin:$(PATH)

ifeq ($(wildcard $(VENV)/bin/pio),)
  PIO := $(shell command -v pio 2>/dev/null || true)
  ifeq ($(PIO),)
    PIO := $(HOME)/.platformio/penv/bin/pio
  endif
else
  PIO := $(VENV)/bin/pio
endif

.PHONY: help setup check build upload monitor merge clean rebuild all \
        build-debug upload-debug flash-debug flash-release \
        debug-device-test debug-device-run \
        native native-build native-clean native-asan check-sdl test test-live test-build compiledb

.DEFAULT_GOAL := help

help: ## Show available targets
	@printf "Plane Radar build targets (PIOENV=%s)\n\n" "$(PIOENV)"
	@grep -E '^[a-zA-Z0-9_.-]+:.*##' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*## "}; {printf "  %-12s %s\n", $$1, $$2}'
	@printf "\nFirst-time setup: make setup\n"

setup: ## Create .venv and install PlatformIO locally
	@command -v $(SYSTEM_PYTHON) >/dev/null 2>&1 || { \
		echo "Error: $(SYSTEM_PYTHON) not found. Install Python 3.10+ and retry." >&2; \
		exit 1; \
	}
	@if [ ! -d "$(VENV)" ]; then \
		echo "==> Creating virtualenv at .venv"; \
		"$(SYSTEM_PYTHON)" -m venv "$(VENV)"; \
	fi
	@echo "==> Upgrading pip"
	@"$(VENV)/bin/pip" install --upgrade pip
	@echo "==> Installing PlatformIO from requirements-dev.txt"
	@"$(VENV)/bin/pip" install --upgrade -r "$(ROOT)/requirements-dev.txt"
	@echo "==> PlatformIO installed:"
	@"$(VENV)/bin/pio" --version
	@echo ""
	@echo "Setup complete. Next: make build"

check: ## Verify Python venv and PlatformIO are available
	@test -x "$(VENV)/bin/pio" || { \
		echo "PlatformIO not found in .venv. Run: make setup" >&2; \
		exit 1; \
	}
	@echo "Python: $$("$(VENV)/bin/python3" --version)"
	@echo "PlatformIO: $$("$(VENV)/bin/pio" --version)"
	@echo "Environment OK"

build: check ## Compile firmware (pio run)
	@"$(PIO)" run -e "$(PIOENV)"

build-debug: check ## Compile debug firmware (-Og -g, supermini_debug)
	@"$(PIO)" run -e supermini_debug

upload-debug: check ## Flash debug firmware via esptool (power-cycle before debugging)
	@"$(PIO)" run -t upload -e supermini_debug

flash-debug: build-debug upload-debug ## Build and flash debug firmware via esptool

flash-release: check ## Build release firmware and flash to device
	@"$(PIO)" run -e "$(PIOENV)" -t upload

debug-device-test: check ## Flash over JTAG, attach GDB, halt at setup()
	@"$(ROOT)/scripts/device-debug.sh" --break setup

debug-device-run: check ## Flash over JTAG, attach GDB, let the board run
	@"$(ROOT)/scripts/device-debug.sh"

compiledb: check ## Refresh compile_commands.json and .clangd for IDE/clangd
	@"$(ROOT)/scripts/generate-compiledb.sh"
	@"$(ROOT)/scripts/generate-clangd.sh"

upload: check ## Flash firmware to the connected board
	@"$(PIO)" run -t upload -e "$(PIOENV)"

monitor: check ## Open serial monitor (115200 baud)
	@"$(PIO)" device monitor -e "$(PIOENV)"

merge: check ## Build merged web-flash image (release/plane-radar-merged.bin)
	@"$(ROOT)/scripts/merge-firmware.sh" --env "$(PIOENV)"

check-sdl: ## Verify SDL2 headers are installed (needed by the native harness)
	@test -f /opt/homebrew/include/SDL2/SDL.h -o -f /usr/local/include/SDL2/SDL.h || { \
		echo "SDL2 headers not found in /opt/homebrew or /usr/local." >&2; \
		echo "Install with: brew install sdl2" >&2; \
		exit 1; \
	}
	@echo "SDL2 OK"

native-build: check check-sdl ## Compile the native harness (no window)
	@"$(PIO)" run -e native

native: native-build ## Compile and run the native harness
	@"$(PIO)" run -e native -t exec

native-asan: check check-sdl ## Run the native harness under ASan/UBSan
	@PLATFORMIO_BUILD_FLAGS="-fsanitize=address,undefined" \
		"$(PIO)" run -e native -t exec

test: check ## Run host unit tests (no hardware needed)
	@"$(PIO)" test -e native_test
	@"$(PIO)" test -e native_test_fetch
	@"$(PIO)" test -e native_test_png

test-live: check ## Live terrain test: real tiles, real PNG decoder (~10s, needs internet)
	@"$(PIO)" test -e native_test_live

test-build: check ## Build host unit tests without running them (SUITE=test_geo picks one)
	@"$(PIO)" test -e native_test --without-testing $(if $(SUITE),-f "$(SUITE)",)

native-clean: ## Remove native build artifacts
	@if test -x "$(PIO)"; then "$(PIO)" run -t clean -e native; \
	else rm -rf "$(ROOT)/.pio/build/native"; fi

clean: ## Remove PlatformIO build artifacts
	@if test -x "$(PIO)"; then "$(PIO)" run -t clean -e "$(PIOENV)"; else rm -rf "$(ROOT)/.pio/build/$(PIOENV)"; fi

rebuild: clean build ## Clean and rebuild

all: build merge ## Build firmware and produce merged release binary
