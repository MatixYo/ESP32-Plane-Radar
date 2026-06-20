#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/**
 * Find the aircraft nearest to a screen point (from the last radar draw).
 * Returns its index into services::adsb::aircraftList(), or -1 if none is
 * within tap range.
 */
int radarDisplayHitTest(int x, int y);

/** Blank the screen and draw the flight details dialog for an aircraft. */
void radarDisplayDrawDialog(int aircraft_index);

}  // namespace ui
