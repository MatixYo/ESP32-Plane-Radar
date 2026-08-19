/**
 * Host tests for platform_png::decode — the allocation-free PNG decoder.
 *
 * Runs in env:native_test_png, which compiles png_decode.cpp and nothing else:
 * no LovyanGFX, no SDL (SDL2 #defines main on macOS, which would rename Unity's
 * entry point), and no platform/native sources — the two seam functions the
 * decoder reaches for are stubbed at the bottom of this file.
 *
 * The fixtures come from scripts/gen_png_fixtures.py, which emits each PNG
 * alongside the exact raster it must decode to and verifies that pairing with
 * zlib before writing the header. So a failure here is a decoder bug: every
 * assertion is on exact pixel values in raster order, not on a decode that
 * merely finished.
 *
 * What the fixture set is built to separate: each of the five row filters, and
 * a sixth image that switches filter per row the way a real encoder does; the
 * three DEFLATE block types (stored, fixed-Huffman, dynamic-Huffman); IDAT
 * spread over several chunks, one boundary landing mid-scanline; ancillary
 * chunks before and after the image data; and a 256x256 tile, the only size the
 * firmware ever asks the bucket for. Then the refusals, which matter as much on
 * a device with no way to report a crash: greyscale, interlaced, truncated,
 * corrupt, and a bad signature must each come back false with the process
 * intact.
 */

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/platform.h"
#include "core/terrain.h"
#include "platform/png_decode.h"
#include "png_fixtures.h"

namespace fx = png_fixtures;
namespace pf = core::platform;

namespace {

// --- The lent scratch ---------------------------------------------------------

/**
 * Scratch plus a tail that was never offered. Borrowing its memory instead of
 * owning any is the decoder's whole reason to exist, and on the device that
 * memory is the frame sprite: a write one byte past the end lands in pixels
 * that get blitted to the panel, not in a segfault. Hence the canary.
 */
constexpr size_t kGuardBytes = 256;
constexpr uint8_t kGuardByte = 0xA5;
uint8_t s_scratch[platform_png::kScratchBytes + kGuardBytes];

bool s_scratch_offered = true;
size_t s_scratch_asked = 0;

uint8_t* lendScratch(size_t need_bytes) {
  s_scratch_asked = need_bytes;
  if (!s_scratch_offered || need_bytes > platform_png::kScratchBytes) {
    return nullptr;
  }
  return s_scratch;
}

bool guardIntact() {
  for (size_t i = platform_png::kScratchBytes; i < sizeof(s_scratch); ++i) {
    if (s_scratch[i] != kGuardByte) {
      return false;
    }
  }
  return true;
}

// --- The pixel sink -----------------------------------------------------------

/**
 * Every callback the decoder made, and the properties that have to hold for all
 * of them. Order is checked against the callback's own index rather than
 * recorded, so a decoder that emits the right pixels bottom-up or column-first
 * fails on order and not only on the raster.
 */
struct Capture {
  uint32_t width = 0;
  uint32_t height = 0;
  size_t count = 0;
  bool bounds_ok = true;
  bool order_ok = true;
  bool ctx_ok = true;
  std::vector<uint8_t> rgb;
};

Capture s_capture;

void capturePixel(void* ctx, uint32_t x, uint32_t y, uint8_t r, uint8_t g,
                  uint8_t b) {
  // Asserting from inside the callback would longjmp out of the decoder and
  // leave it mid-image, so everything is recorded and judged afterwards.
  if (ctx != &s_capture) {
    s_capture.ctx_ok = false;
    return;
  }
  Capture& cap = s_capture;
  const size_t index = cap.count++;
  if (x >= cap.width || y >= cap.height) {
    cap.bounds_ok = false;
  }
  if (x != index % cap.width || y != index / cap.width) {
    cap.order_ok = false;
  }
  cap.rgb.push_back(r);
  cap.rgb.push_back(g);
  cap.rgb.push_back(b);
}

bool decodeFixture(const fx::Fixture& fixture) {
  s_capture = Capture{};
  s_capture.width = fixture.width;
  s_capture.height = fixture.height;
  pf::MemoryBodyReader body(reinterpret_cast<const char*>(fixture.png),
                            fixture.png_len);
  return platform_png::decode(body, capturePixel, &s_capture);
}

/** Pixel count, raster order and bounds — true of every image that decodes. */
void assertRasterShape(const fx::Fixture& fixture) {
  const size_t expected = static_cast<size_t>(fixture.width) * fixture.height;
  char msg[128];
  snprintf(msg, sizeof(msg), "%s pixel count", fixture.name);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(expected, s_capture.count, msg);
  snprintf(msg, sizeof(msg), "%s wrote outside the image", fixture.name);
  TEST_ASSERT_TRUE_MESSAGE(s_capture.bounds_ok, msg);
  snprintf(msg, sizeof(msg), "%s pixels not in raster order", fixture.name);
  TEST_ASSERT_TRUE_MESSAGE(s_capture.order_ok, msg);
  snprintf(msg, sizeof(msg), "%s ctx not passed through", fixture.name);
  TEST_ASSERT_TRUE_MESSAGE(s_capture.ctx_ok, msg);
  snprintf(msg, sizeof(msg), "%s wrote past the scratch it was lent",
           fixture.name);
  TEST_ASSERT_TRUE_MESSAGE(guardIntact(), msg);
}

void assertDecodes(const fx::Fixture& fixture) {
  TEST_ASSERT_TRUE_MESSAGE(decodeFixture(fixture), fixture.name);
  assertRasterShape(fixture);

  TEST_ASSERT_NOT_NULL_MESSAGE(fixture.rgb, fixture.name);
  TEST_ASSERT_EQUAL_UINT(fixture.rgb_len, s_capture.rgb.size());

  char msg[128];
  for (uint32_t y = 0; y < fixture.height; ++y) {
    for (uint32_t x = 0; x < fixture.width; ++x) {
      const size_t at = (static_cast<size_t>(y) * fixture.width + x) * 3;
      snprintf(msg, sizeof(msg), "%s pixel (%u,%u)", fixture.name, x, y);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(fixture.rgb[at], s_capture.rgb[at], msg);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(fixture.rgb[at + 1],
                                     s_capture.rgb[at + 1], msg);
      TEST_ASSERT_EQUAL_HEX8_MESSAGE(fixture.rgb[at + 2],
                                     s_capture.rgb[at + 2], msg);
    }
  }
}

void assertRejected(const fx::Fixture& fixture) {
  TEST_ASSERT_FALSE_MESSAGE(decodeFixture(fixture), fixture.name);

  // A refusal is allowed to happen late — a truncated body can deliver the rows
  // that did arrive — but never outside the image and never past the scratch.
  char msg[128];
  snprintf(msg, sizeof(msg), "%s wrote outside the image", fixture.name);
  TEST_ASSERT_TRUE_MESSAGE(s_capture.bounds_ok, msg);
  snprintf(msg, sizeof(msg), "%s pixels not in raster order", fixture.name);
  TEST_ASSERT_TRUE_MESSAGE(s_capture.order_ok, msg);
  snprintf(msg, sizeof(msg), "%s wrote past the scratch it was lent",
           fixture.name);
  TEST_ASSERT_TRUE_MESSAGE(guardIntact(), msg);
}

uint64_t fnv1a64(const std::vector<uint8_t>& data) {
  uint64_t digest = fx::kFnvOffsetBasis;
  for (const uint8_t byte : data) {
    digest = (digest ^ byte) * fx::kFnvPrime;
  }
  return digest;
}

}  // namespace

// --- Platform stubs (this env links no platform/native sources) ---------------

namespace core::platform {

// Silent: the decoder logs a line per rejected image, and the point of the
// reject tests is that they all run in one go and stay readable.
void logf(const char*, ...) {}

unsigned long nowMs() { return 0; }

}  // namespace core::platform

// --- Row filters -------------------------------------------------------------

// The five fixtures below encode one raster five ways, so a failure in exactly
// one of them names the predictor that is wrong. Their channel values run past
// 0 and 255 between neighbours: every filter is defined modulo 256, and a
// decoder that clamps or promotes to signed decodes a smooth gradient perfectly
// and these not at all.

void test_filter_none(void) { assertDecodes(fx::kFilterNone); }

void test_filter_sub(void) { assertDecodes(fx::kFilterSub); }

void test_filter_up(void) { assertDecodes(fx::kFilterUp); }

void test_filter_average(void) { assertDecodes(fx::kFilterAverage); }

void test_filter_paeth(void) { assertDecodes(fx::kFilterPaeth); }

void test_a_different_filter_on_every_row(void) {
  // What real encoders emit: the filter is chosen per scanline. A decoder that
  // reads the filter byte once gets row 0 right and then drifts.
  assertDecodes(fx::kMixedFilters);
}

// --- DEFLATE block types -----------------------------------------------------

void test_stored_deflate_blocks(void) {
  // No Huffman decoding at all, so this isolates block framing and the copy.
  assertDecodes(fx::kStoredDeflate);
}

void test_fixed_huffman_blocks(void) {
  // No tree is transmitted; the code lengths are the ones RFC 1951 fixes, and
  // the decoder has to supply them itself.
  assertDecodes(fx::kFixedHuffman);
}

void test_dynamic_huffman_blocks(void) {
  // Trees built from the code-length alphabet in the block header, run-length
  // escapes included. This is what the real tiles are.
  assertDecodes(fx::kDynamicHuffman);
}

// --- Chunk layout ------------------------------------------------------------

void test_idat_split_across_chunks(void) {
  // Five IDAT chunks, one of them zero-length: a tile off the bucket arrives
  // chopped wherever the encoder's buffer ran out, and an empty IDAT is legal.
  assertDecodes(fx::kSplitIdat);
}

void test_idat_boundary_inside_a_scanline(void) {
  // Half a scanline has to be carried across the chunk boundary.
  assertDecodes(fx::kSplitIdatMidScanline);
}

void test_ancillary_chunks_are_skipped(void) {
  // pHYs and tEXt ahead of the image data, another tEXt after it. All three
  // have to be walked past on chunk length alone.
  assertDecodes(fx::kAncillaryChunks);
}

// --- The real tile size ------------------------------------------------------

void test_full_size_terrarium_tile(void) {
  const fx::Fixture& fixture = fx::kTerrariumTile;
  TEST_ASSERT_TRUE_MESSAGE(decodeFixture(fixture), fixture.name);
  assertRasterShape(fixture);

  // 196608 bytes of expected raster would dwarf the rest of the fixture
  // header, so the tile is pinned by a hash over the RGB bytes in callback
  // order — which fixes values and order together — with its first and last
  // row emitted in full to make a failure readable.
  const size_t stride = static_cast<size_t>(fixture.width) * 3;
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fx::kTerrariumTileFirstRowRgb,
                               s_capture.rgb.data(), stride);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fx::kTerrariumTileLastRowRgb,
                               s_capture.rgb.data() + (fixture.height - 1) * stride,
                               stride);

  const uint64_t got = fnv1a64(s_capture.rgb);
  if (got != fx::kTerrariumTileRgbHash) {
    char msg[128];
    snprintf(msg, sizeof(msg), "tile raster hash %016llx, expected %016llx",
             static_cast<unsigned long long>(got),
             static_cast<unsigned long long>(fx::kTerrariumTileRgbHash));
    TEST_FAIL_MESSAGE(msg);
  }
}

// --- Nothing lands outside the image ----------------------------------------

void test_no_callback_lands_outside_the_image(void) {
  // Stated once over every fixture rather than left implicit in the per-image
  // tests: the sink writes straight into the elevation grid, where an x or y
  // past the edge is a stray write into whatever is next in RAM.
  for (const fx::Fixture& fixture : fx::kDecodable) {
    TEST_ASSERT_TRUE_MESSAGE(decodeFixture(fixture), fixture.name);
    assertRasterShape(fixture);
  }
}

// --- Borrowed memory ---------------------------------------------------------

void test_no_scratch_is_a_clean_failure(void) {
  // On the device the scratch is the frame sprite, and it can be genuinely
  // unavailable. That has to read as "no terrain this time", not as a decode
  // into a null pointer.
  s_scratch_offered = false;

  TEST_ASSERT_FALSE(decodeFixture(fx::kDynamicHuffman));
  TEST_ASSERT_EQUAL_UINT(0, s_capture.count);
}

void test_decoder_stays_inside_the_scratch_it_was_lent(void) {
  assertDecodes(fx::kFilterPaeth);
  TEST_ASSERT_TRUE(s_scratch_asked > 0);
  TEST_ASSERT_TRUE_MESSAGE(s_scratch_asked <= platform_png::kScratchBytes,
                           "asked for more than kScratchBytes advertises");

  // The largest image the firmware ever decodes, i.e. the one that comes
  // closest to filling the two-scanline half of the budget.
  TEST_ASSERT_TRUE(decodeFixture(fx::kTerrariumTile));
  TEST_ASSERT_TRUE(s_scratch_asked <= platform_png::kScratchBytes);
  TEST_ASSERT_TRUE(guardIntact());
}

// --- What has to be refused --------------------------------------------------

void test_greyscale_is_rejected(void) {
  assertRejected(fx::kRejectGreyscale);
  // Colour type is in IHDR, so the refusal cannot come late.
  TEST_ASSERT_EQUAL_UINT(0, s_capture.count);
}

void test_interlaced_is_rejected(void) {
  // Adam7 would need the whole raster in RAM, which is the one thing this
  // decoder does not have. Also refused from IHDR.
  assertRejected(fx::kRejectInterlaced);
  TEST_ASSERT_EQUAL_UINT(0, s_capture.count);
}

void test_truncated_body_is_rejected(void) {
  // A dropped connection mid-tile. Whatever rows arrived, the image did not.
  assertRejected(fx::kRejectTruncated);
}

void test_corrupt_deflate_is_rejected(void) {
  // Bytes flipped inside the compressed stream with the chunk CRC recomputed,
  // so the failure has to come out of inflate rather than a checksum before it.
  assertRejected(fx::kRejectCorruptDeflate);
}

void test_bad_signature_is_rejected(void) {
  assertRejected(fx::kRejectBadSignature);
  TEST_ASSERT_EQUAL_UINT(0, s_capture.count);
}

void setUp(void) {
  memset(s_scratch, kGuardByte, sizeof(s_scratch));
  s_scratch_offered = true;
  s_scratch_asked = 0;
  s_capture = Capture{};
  platform_png::setScratch(lendScratch);
}

void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_filter_none);
  RUN_TEST(test_filter_sub);
  RUN_TEST(test_filter_up);
  RUN_TEST(test_filter_average);
  RUN_TEST(test_filter_paeth);
  RUN_TEST(test_a_different_filter_on_every_row);

  RUN_TEST(test_stored_deflate_blocks);
  RUN_TEST(test_fixed_huffman_blocks);
  RUN_TEST(test_dynamic_huffman_blocks);

  RUN_TEST(test_idat_split_across_chunks);
  RUN_TEST(test_idat_boundary_inside_a_scanline);
  RUN_TEST(test_ancillary_chunks_are_skipped);

  RUN_TEST(test_full_size_terrarium_tile);
  RUN_TEST(test_no_callback_lands_outside_the_image);

  RUN_TEST(test_no_scratch_is_a_clean_failure);
  RUN_TEST(test_decoder_stays_inside_the_scratch_it_was_lent);

  RUN_TEST(test_greyscale_is_rejected);
  RUN_TEST(test_interlaced_is_rejected);
  RUN_TEST(test_truncated_body_is_rejected);
  RUN_TEST(test_corrupt_deflate_is_rejected);
  RUN_TEST(test_bad_signature_is_rejected);

  return UNITY_END();
}
