#pragma once

/**
 * The shared display handle.
 *
 * Both destinations expose the same `tft` object and the same displayInit();
 * only the underlying LGFX panel differs — GC9A01 over SPI on the device, SDL
 * on the host. This is the single sanctioned #ifdef in shared code; every other
 * platform difference is a whole file selected by build_src_filter.
 */

#if defined(PLANE_RADAR_NATIVE)
#include "platform/native/lgfx_config_native.hpp"
#else
#include "platform/device/lgfx_config_device.hpp"
#endif

extern LGFX tft;

void displayInit();
