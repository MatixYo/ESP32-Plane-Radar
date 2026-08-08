#pragma once

/**
 * LovyanGFX device for the native destination.
 *
 * Deliberately declares no class of its own. With -DLGFX_AUTODETECT and SDL
 * headers on the include path, LovyanGFX.hpp reaches LGFX_AutoDetect_sdl.hpp,
 * which defines lgfx::LGFX (a Panel_sdl-backed LGFX_Device) and ends with
 * `using LGFX = lgfx::LGFX;` at global scope. Declaring our own `class LGFX`
 * here would be a hard redefinition of that name.
 *
 * The library's class already does everything the device config does by hand:
 * it sets memory/panel width and height, calls setScaling() before setPanel(),
 * sets the board id, and overrides init_impl to skip the reset sequence.
 * Construct it as LGFX(width, height, scale_x, scale_y).
 */

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
