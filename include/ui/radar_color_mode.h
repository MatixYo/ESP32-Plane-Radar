#pragma once

#include <cstdint>

namespace ui::radar {

enum ColorMode : uint8_t { kColorModeNight = 0, kColorModeDay = 1 };

/** Load saved color mode from flash. Call once at boot (via rangeInit). */
void colorModeInit();

ColorMode colorMode();
bool isDayMode();

/** Toggle night/day, save to flash, log to serial. */
void colorModeToggle();

}  // namespace ui::radar
