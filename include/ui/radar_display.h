#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw satellites only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshSatellites();

}  // namespace ui
