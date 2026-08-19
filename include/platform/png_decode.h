#pragma once

/**
 * Streaming PNG decoder for the terrain layer, with its own inflate and NOT ONE
 * BYTE of its own memory: the caller lends it the scratch it needs.
 *
 * That is the whole reason this exists instead of a call into LovyanGFX's
 * bundled pngle. On the ESP32-C3 the three things a terrain download wants — the
 * 115 KB frame sprite, a ~30 KB TLS session and a ~44 KB decoder — add up to
 * about 13 KB more than the heap has, and freeing the sprite for the duration
 * does not work: something in the TLS/TCP path allocates a few hundred bytes
 * inside the hole it leaves (measured largest free block 114,676 against the
 * 115,200 wanted) and TIME_WAIT keeps it there, so the sprite never comes back
 * and every later frame has to be painted straight onto the panel.
 *
 * A decoder that borrows memory dissolves the conflict: the frame sprite is
 * allocated once at boot and never freed, and while a tile is being decoded its
 * pixels — which no one is looking at, the panel holds the last frame that was
 * blitted — are the decoder's workspace. Nothing is allocated at run time at
 * all, so nothing can fail to be allocated.
 *
 * Lives in platform/ because core/ must not include LovyanGFX; main.cpp hands
 * decode() to core::terrain::setPngDecoder() and points setScratch() at the
 * sprite.
 */

#include <cstddef>
#include <cstdint>

#include "core/platform.h"
#include "core/terrain.h"

namespace platform_png {

/**
 * Scratch the decoder requires, in one contiguous piece:
 *
 *   32768  the DEFLATE sliding window, whose size RFC 1951 fixes
 *    2048  two unfiltered scanlines (a 256 px RGB row is 768 bytes)
 *    1024  Huffman decode tables for the literal, distance and length trees
 *
 * A 240x240 16-bit sprite is 115,200 bytes, so it has room to spare.
 */
constexpr size_t kScratchBytes = 32768 + 2048 + 1024;

/**
 * Lends the decoder `need_bytes` of scratch, or nullptr when it cannot. Called
 * once per image, so what is handed back need only be untouched by anyone else
 * until that image is decoded — nothing is retained between calls.
 */
using ScratchFn = uint8_t* (*)(size_t need_bytes);
void setScratch(ScratchFn fn);

/**
 * Decode a PNG straight off an HTTP body, invoking `on_pixel` once per pixel in
 * raster order. Only the window and two scanlines are held, so the raster never
 * exists in memory and tile size costs nothing.
 *
 * Accepts what the terrain tiles are — 8-bit truecolour RGB, non-interlaced, up
 * to 256 px wide — and rejects anything else rather than guessing. Returns false
 * on a malformed or truncated image, and when no scratch is available.
 */
bool decode(core::platform::BodyReader& body, core::terrain::PixelFn on_pixel,
            void* ctx);

}  // namespace platform_png
