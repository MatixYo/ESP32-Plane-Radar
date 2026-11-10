#pragma once

namespace ui {

/**
 * Startup photo slideshow with a curved caption along the top edge. Blocks
 * until it has finished, leaving the screen black. Call right after
 * displayInit().
 */
void photoSplashShow();

}  // namespace ui