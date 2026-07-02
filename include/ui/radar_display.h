#pragma once

#include "services/route.h"

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Advance and draw the rotating radar sweep. Call frequently from loop(). */
void radarDisplayAnimate();

/**
 * Find the aircraft nearest to a screen point (from the last radar draw).
 * Returns its index into services::adsb::aircraftList(), or -1 if none is
 * within tap range.
 */
int radarDisplayHitTest(int x, int y);

/**
 * Blank the screen and draw the flight details dialog for an aircraft.
 * route may be nullptr (or invalid) if no departure/arrival info is available.
 */
void radarDisplayDrawDialog(int aircraft_index,
                            const services::route::RouteInfo* route);

}  // namespace ui
