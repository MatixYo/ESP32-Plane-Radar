#pragma once

#include <cstddef>

namespace ui::marquee {

/** A message starts this often (start to start). */
inline constexpr unsigned long kIntervalMs = 30UL * 60UL * 1000UL;
/** ...and keeps rolling for this long. */
inline constexpr unsigned long kDurationMs = 2UL * 60UL * 1000UL;
/** First message this long after the radar first appears (so you can see it
 *  soon after boot). Set equal to kIntervalMs to wait the full 30 minutes. */
inline constexpr unsigned long kFirstDelayMs = 15UL * 1000UL;
/** Scroll speed along the edge (pixels per second). */
inline constexpr float kSpeedPxPerSec = 40.0f;
/** Redraw interval while a message is rolling (smooth motion needs more than
 *  the normal 4 Hz radar refresh). */
inline constexpr unsigned long kRedrawIntervalMs = 100;

/**
 * Messages to pick from at random (a different one from last time).
 * Plain letters, digits and basic punctuation only; keep each under ~60
 * characters. Edit freely.
 */
inline constexpr const char* kMessages[] = {
    "Always thinking of you <3",
    "I love your laugh <3",
    "Your smile makes my day <3",
    "You are my favourite person <3",
    "Can't wait till I see you <3",
    "I really really like you <3",
    "You are really imporant to me <3",
};
inline constexpr size_t kMessageCount = sizeof(kMessages) / sizeof(kMessages[0]);

}  // namespace ui::marquee