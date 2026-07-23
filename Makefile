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

.PHONY: help setup check build upload monitor merge clean rebuild all

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

upload: check ## Flash firmware to the connected board
	@"$(PIO)" run -t upload -e "$(PIOENV)"

monitor: check ## Open serial monitor (115200 baud)
	@"$(PIO)" device monitor -e "$(PIOENV)"

merge: check ## Build merged web-flash image (release/plane-radar-merged.bin)
	@"$(ROOT)/scripts/merge-firmware.sh --env "$(PIOENV)"

clean: ## Remove PlatformIO build artifacts
	@if test -x "$(PIO)"; then "$(PIO)" run -t clean -e "$(PIOENV)"; else rm -rf "$(ROOT)/.pio/build/$(PIOENV)"; fi

rebuild: clean build ## Clean and rebuild

all: build merge ## Build firmware and produce merged release binary
