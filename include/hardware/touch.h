#pragma once

/**
 * CST816S capacitive touch (Waveshare ESP32-S3-Touch-LCD-1.28 only).
 *
 * On boards without a touch panel (e.g. ESP32-C3 Super Mini) both functions
 * compile to no-ops, so callers need no #ifdefs.
 */

/** Init I2C + reset the controller. Safe to call once in setup(). */
void touchInit();

/**
 * Poll the panel; returns true once per finger-down (a "tap"). A held finger
 * does not repeat — the next tap needs a lift first.
 */
bool touchConsumeTap();
