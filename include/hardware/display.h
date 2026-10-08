#pragma once

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

void displayInit();

/**
 * Block until the panel's tearing-effect line says a new frame is starting, so
 * a full-screen push lands in the vertical blanking interval instead of partway
 * down a visible frame. Returns immediately when no TE pin is wired, and gives
 * up after a short timeout so a miswired line cannot stall the main loop.
 *
 * LovyanGFX has no TE support for SPI panels -- getScanLine() returns -1 for
 * all of them -- so this is polled here rather than by the driver.
 */
void displayWaitForFrameStart();
