#pragma once

/** GT911 pinch-to-zoom for radar range (Waveshare ESP32-P4 4C). */

void touchGestureInit();

/** Poll touch; returns +1 zoom out (larger range), -1 zoom in (smaller range), 0 none. */
int touchGesturePollPinchZoom();
