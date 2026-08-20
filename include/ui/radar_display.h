#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Free the off-screen frame sprite (~112 KB) so HTTPS/TLS can allocate. */
void radarDisplaySuspendFrameBuffer();

}  // namespace ui
