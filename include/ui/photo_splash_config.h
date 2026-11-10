#pragma once

namespace ui::photosplash {

/** Text curved along the top edge of EVERY photo (same style as the rolling
 *  messages). Use "" for no text. Plain letters/numbers/punctuation only. */
inline constexpr char kCaption[] = "I Love You ~ Danny";

/** How long each photo stays on screen (ms), not counting the opening wipe. */
inline constexpr unsigned long kHoldMs = 1800;
/** The last photo stays a little longer (ms). */
inline constexpr unsigned long kLastHoldMs = 2600;

/** Opening wipe between photos: steps and pause per step (ms). */
inline constexpr int kWipeSteps = 6;
inline constexpr unsigned long kWipeStepMs = 15;

/** Caption text colour (RGB) and its outline (makes it readable on any photo). */
inline constexpr unsigned char kTextR = 255, kTextG = 255, kTextB = 255;

/** Caption size multiplier on the 9pt bold font. */
inline constexpr float kTextScale = 1.2f;
/** Distance from the very edge of the screen to the top of the letters (px). */
inline constexpr int kEdgeMarginPx = 5;

}  // namespace ui::photosplash