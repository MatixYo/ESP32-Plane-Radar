#pragma once

namespace ui {

/**
 * Boot splash: a heart on a black background with a message inside, held for
 * about 3 seconds in total, then faded out to black. Blocks until finished.
 * Call once in setup() after displayInit().
 */
void splashScreenShow();

}  // namespace ui