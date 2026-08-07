#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Refresh only the NM-TV-154 update-age value without redrawing the radar. */
void radarDisplayRefreshStatus();

/** Record the time of the most recent successful ADS-B response. */
void radarDisplayMarkDataUpdated(unsigned long now_ms);

}  // namespace ui
