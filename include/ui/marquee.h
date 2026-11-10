#pragma once

#include <LovyanGFX.hpp>

namespace ui {

/**
 * True while a message is rolling. Also advances the schedule, so call it from
 * the main loop. Use it to redraw faster while true.
 */
bool marqueeActive();

/**
 * Draw the rolling message along the top edge of the round display, on top of
 * the radar. Does nothing outside a message window. Call after the radar is
 * drawn into `gfx`.
 */
void marqueeDraw(lgfx::LGFXBase& gfx);

}  // namespace ui