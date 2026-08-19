/**
 * Live smoke test for the terrain layer against the REAL tile bucket.
 *
 * NEEDS THE INTERNET and is opt-in: `make test-live` only, never `make test`.
 * It runs in env:native_test_live with the real http_curl.cpp transport, the
 * real clock, and — unlike every other host test — the REAL PNG decoder
 * (platform/png_decode.cpp). Nothing is faked, so this is the full path the
 * firmware takes: build a URL, download a terrain-RGB tile off
 * s3.amazonaws.com/elevation-tiles-prod, decode it streaming, and resample it
 * into the elevation grid.
 *
 * The one thing that cannot be the same here is where the decoder's scratch
 * comes from: on the device the display lends it the frame sprite's pixels, and
 * there is no display in this process. test_png covers the decoder itself
 * against fixtures; what this suite adds is the network and real terrain.
 *
 * Everything on our side of the wire is covered deterministically in
 * test_terrain_fetch. What only this suite can prove is that the bucket accepts
 * our URLs, that its PNGs decode with the decoder we ship, and that the numbers
 * that come out are real terrain. Hence the deliberately generous elevation
 * bands: the claim under test is "this is the right mountain", not a DEM value.
 *
 * Two views, so a handful of tiles at most — it finishes in seconds.
 */

#include <unity.h>

#include <cstddef>
#include <cstring>

#include "core/platform.h"
#include "core/terrain.h"
#include "platform/png_decode.h"

namespace ct = core::terrain;
namespace pf = core::platform;

namespace {

/** Graz, Austria: city basin at ~350-400 m, Alps rising to the north-west. */
constexpr double kGrazLat = 47.0753;
constexpr double kGrazLon = 15.4062;

/** Grossglockner, Austria's highest summit at 3798 m. */
constexpr double kAlpineLat = 47.0747;
constexpr double kAlpineLon = 12.6947;

/** ~40 km to the screen edge, which resolves to a zoom-8 view here. */
constexpr float kSpan = 40.0f;
constexpr uint8_t kRange = 1;

/** kTerrainGridSize is odd, so one sample sits exactly on the centre. */
constexpr int kCenterIndex =
    (ct::kGridSize / 2) * ct::kGridSize + ct::kGridSize / 2;

/**
 * Download the whole grid the way the main loop does. Generous deadline: it
 * has to survive a slow tile, and a tile that fails three times parks the
 * download behind kTerrainRetryIntervalMs.
 */
bool downloadGrid(double lat, double lon) {
  const unsigned long deadline_ms = pf::nowMs() + 120000;
  while (pf::nowMs() < deadline_ms) {
    if (ct::ensureGrid(lat, lon, kRange, kSpan)) {
      return true;
    }
    pf::sleepMs(50);
  }
  return false;
}

/** Stands in for the frame sprite the firmware lends the decoder. */
uint8_t s_scratch[platform_png::kScratchBytes];

uint8_t* lendScratch(size_t need_bytes) {
  return need_bytes <= sizeof(s_scratch) ? s_scratch : nullptr;
}

int16_t gridMax(const ct::Grid* g) {
  int16_t max_m = g->elev_m[0];
  for (int i = 0; i < ct::kGridPoints; ++i) {
    if (g->elev_m[i] > max_m) {
      max_m = g->elev_m[i];
    }
  }
  return max_m;
}

}  // namespace

// --- The URL we ask the bucket for -------------------------------------------

void test_live_tile_url_shape(void) {
  ct::TileId tile;
  tile.z = 8;
  tile.x = 138;
  tile.y = 90;
  char url[160];
  ct::buildTileUrl(url, sizeof(url), tile);

  // The bucket's own layout, z/x/y in that order — the two grid tests below
  // would only tell us "no data" if this were wrong. The URL scheme is
  // deliberately not pinned here; that is config.h's call, not the bucket's.
  TEST_ASSERT_NOT_NULL_MESSAGE(
      strstr(url, "s3.amazonaws.com/elevation-tiles-prod/terrarium/8/138/90.png"),
      url);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(url, "://"), url);
}

// --- Real tiles, real decoder, plausible terrain -----------------------------

void test_live_graz_grid_is_plausible(void) {
  TEST_ASSERT_TRUE_MESSAGE(downloadGrid(kGrazLat, kGrazLon),
                           "grid did not complete — offline, or the bucket "
                           "rejected our URL?");
  TEST_ASSERT_TRUE(ct::gridReady(kGrazLat, kGrazLon, kRange));

  const ct::Grid* g = ct::grid(kRange);
  TEST_ASSERT_NOT_NULL(g);

  // Graz sits around 350-400 m. A band this wide still fails outright on a
  // lat/lon swap, a terrarium offset error, or a feet/metres mix-up.
  const int16_t center_m = g->elev_m[kCenterIndex];
  TEST_ASSERT_GREATER_THAN_INT16(200, center_m);
  TEST_ASSERT_LESS_THAN_INT16(700, center_m);

  // 40 km around Graz reaches into the Alps, so the DEM must show relief —
  // a decode that produced one constant would pass the band check alone.
  TEST_ASSERT_GREATER_THAN_INT16(800, gridMax(g));
}

void test_live_alpine_grid_is_high(void) {
  TEST_ASSERT_TRUE_MESSAGE(downloadGrid(kAlpineLat, kAlpineLon),
                           "grid did not complete — offline, or the bucket "
                           "rejected our URL?");

  const ct::Grid* g = ct::grid(kRange);
  TEST_ASSERT_NOT_NULL(g);

  // The high Tauern: even the valley floors here are above 1200 m, and the
  // centre sample lands on the Grossglockner massif itself.
  TEST_ASSERT_GREATER_THAN_INT16(1500, g->elev_m[kCenterIndex]);
  TEST_ASSERT_GREATER_THAN_INT16(2500, gridMax(g));
}

void setUp(void) {
  ct::clear();
  ct::setPngDecoder(platform_png::decode);
  platform_png::setScratch(lendScratch);
}

void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_live_tile_url_shape);
  RUN_TEST(test_live_graz_grid_is_plausible);
  RUN_TEST(test_live_alpine_grid_is_high);

  return UNITY_END();
}
