#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/**
 * Lends out the frame sprite's pixels as scratch, or nullptr if there are fewer
 * than `need_bytes` of them (or no sprite at all).
 *
 * The terrain PNG decoder is the borrower. Its working set does not fit
 * alongside the sprite and a TLS session, and freeing the sprite to make room
 * does not work — the 115 KB never comes back, because the TLS path leaves a
 * few hundred bytes stranded inside the hole. Lending instead of freeing costs
 * nothing: while a tile is decoding nobody is compositing a frame, and the panel
 * is showing the last one that was blitted. The contents come back as garbage,
 * so the caller must repaint the whole frame afterwards.
 */
uint8_t* radarDisplayFrameScratch(size_t need_bytes);

}  // namespace ui
