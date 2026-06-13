#pragma once

/** GT911 touch gestures (Waveshare ESP32-P4 4C). */

void touchGestureInit();

struct TouchGestureEvent {
  /** +1 zoom out (larger range), -1 zoom in, 0 none. */
  int pinch_zoom = 0;
  /** Triple single-finger tap toggles day/night palette. */
  bool color_mode_toggle = false;
};

/** One GT911 read per call — pinch and tap do not compete for the buffer. */
TouchGestureEvent touchGesturePoll();
