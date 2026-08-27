#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/**
 * Advance the take-turns animation for overlapping aircraft tags. Call often
 * from loop(); cheap no-op when nothing overlaps or the current 2 s turn has
 * not elapsed yet.
 */
void radarDisplayAnimTick();

}  // namespace ui
