#pragma once

#include "hardware/display.h"

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Set selection state for target cycling and highlighted rendering on radar. */
void radarSetSelection(bool select_mode, int selected_aircraft_idx);

/** Shared offscreen sprite buffer to avoid duplicate heap allocation. */
lgfx::LGFX_Sprite& sharedFrameSprite();
bool ensureSharedFrameSprite();

}  // namespace ui
