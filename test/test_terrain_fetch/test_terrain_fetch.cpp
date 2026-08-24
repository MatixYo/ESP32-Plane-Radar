/**
 * Host tests for core::terrain's tile download state machine and resampler.
 *
 * Runs in env:native_test_fetch, which links neither http_curl.cpp nor
 * platform_native.cpp: this file provides a scripted HttpClient::get and a
 * hand-cranked clock instead. The PNG decode is injected too
 * (ct::setPngDecoder), so the whole flow runs with no network, no real clock
 * and no PNG library — the fake decoder synthesises a flat terrarium tile
 * whose elevation identifies the tile it came from.
 *
 * That makes deterministic everything that would otherwise only happen against
 * the live tile bucket: one request per tile in tilesForView() order, the
 * spacing between them, where each decoded tile lands in the grid, retrying a
 * failed tile without losing the tiles already decoded, abandoning a download
 * that keeps failing, restarting when the view changes mid-download, and the
 * single-slot cache.
 */

#include <unity.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "config.h"
#include "core/platform.h"
#include "core/terrain.h"

namespace ct = core::terrain;
namespace pf = core::platform;

namespace {

unsigned long s_fake_now_ms = 0;

/** Requests as issued; outcome script consumed one entry per request. */
std::vector<std::string> s_urls;
std::deque<bool> s_outcomes;  ///< empty = every request succeeds

void advanceMs(unsigned long ms) { s_fake_now_ms += ms; }

/** Base search centre: Graz, with the Alps to the north-west. */
constexpr double kBaseLat = 47.0753;
constexpr double kBaseLon = 15.4062;
/** ~55 km to the screen edge, i.e. the wide end of the range presets. */
constexpr float kSpan = 55.4f;
constexpr uint8_t kRange = 1;

/** A view plus the tiles core::terrain will ask for to cover it. */
struct View {
  double lat = 0.0;
  double lon = 0.0;
  float span = 0.0f;
  int zoom = 0;
  ct::TileId tiles[ct::kMaxTiles];
  size_t tile_count = 0;
};

View makeView(double lat, double lon, float span) {
  View v;
  v.lat = lat;
  v.lon = lon;
  v.span = span;
  v.zoom = ct::zoomForView(lat, lon, span);
  v.tile_count =
      ct::tilesForView(lat, lon, span, v.zoom, v.tiles, ct::kMaxTiles);
  return v;
}

/**
 * A view's tile count depends on how its bounding box happens to straddle the
 * tile grid, so the 1-tile and 4-tile cases are searched for rather than
 * hard-coded: shifting the centre in whole steps of ~5 km walks the box across
 * a tile boundary without changing the zoom the view resolves to.
 */
bool findView(size_t want, float span, View* out) {
  for (int i = 0; i < 40; ++i) {
    for (int j = 0; j < 60; ++j) {
      const View v =
          makeView(kBaseLat + 0.05 * i, kBaseLon + 0.05 * j, span);
      if (v.tile_count == want) {
        *out = v;
        return true;
      }
    }
  }
  return false;
}

std::string urlOf(const ct::TileId& tile) {
  char buf[160];
  ct::buildTileUrl(buf, sizeof(buf), tile);
  return buf;
}

/**
 * Every pixel of a tile carries this elevation, so a grid sample names the
 * tile it was resampled from. Adjacent tiles differ (7, 13 and 20 are all
 * coprime with the modulus), and it is never 0 — the value a sample left
 * untouched by the resampler would keep.
 */
int16_t elevForTile(const ct::TileId& tile) {
  return static_cast<int16_t>(500 + (tile.x * 7 + tile.y * 13) % 3000);
}

/**
 * z/x/y of a tile URL: the last three integer runs in it. Read out of the URL
 * rather than passed alongside it because the fake decoder sees only the body,
 * which is where the fake transport puts the URL text.
 */
bool parseTileUrl(const std::string& url, ct::TileId* out) {
  std::vector<int> nums;
  for (size_t i = 0; i < url.size();) {
    if (std::isdigit(static_cast<unsigned char>(url[i])) == 0) {
      ++i;
      continue;
    }
    int value = 0;
    while (i < url.size() &&
           std::isdigit(static_cast<unsigned char>(url[i])) != 0) {
      value = value * 10 + (url[i] - '0');
      ++i;
    }
    nums.push_back(value);
  }
  if (nums.size() < 3) {
    return false;
  }
  out->z = nums[nums.size() - 3];
  out->x = nums[nums.size() - 2];
  out->y = nums[nums.size() - 1];
  return true;
}

/**
 * Stand-in for platform_png::decode: synthesises a 256x256 terrarium tile,
 * every pixel encoding elevForTile() with the inverse of terrariumElevation().
 */
int s_decodes = 0;

bool fakeDecode(pf::BodyReader& body, ct::PixelFn on_pixel, void* ctx) {
  ++s_decodes;

  std::string url;
  char buf[128];
  size_t n = 0;
  while ((n = body.readBytes(buf, sizeof(buf))) > 0) {
    url.append(buf, n);
  }

  ct::TileId tile;
  if (!parseTileUrl(url, &tile)) {
    return false;
  }
  const int encoded = elevForTile(tile) + 32768;
  const uint8_t r = static_cast<uint8_t>(encoded >> 8);
  const uint8_t g = static_cast<uint8_t>(encoded & 0xFF);
  for (uint32_t y = 0; y < ct::kTilePx; ++y) {
    for (uint32_t x = 0; x < ct::kTilePx; ++x) {
      on_pixel(ctx, x, y, r, g, 0);
    }
  }
  return true;
}

/** Crank calls and clock until the download completes (or give up). */
bool runToCompletion(const View& v, uint8_t range) {
  for (int i = 0; i < 4 * ct::kMaxTiles; ++i) {
    if (ct::ensureGrid(v.lat, v.lon, range, v.span)) {
      return true;
    }
    advanceMs(config::kTerrainTileIntervalMs);
  }
  return false;
}

/**
 * Every grid sample carries the elevation of the tile its own pixel falls in,
 * and none was left at sea level. Recomputed from the pure helpers, so a
 * resampler that dropped rows, mixed up axes or mis-offset a tile shows up as
 * a mismatch rather than as plausible-looking terrain.
 */
void assertGridMatchesTiles(const View& v) {
  const ct::Grid* g = ct::grid(kRange);
  TEST_ASSERT_NOT_NULL(g);

  char msg[96];
  for (int row = 0; row < ct::kGridSize; ++row) {
    for (int col = 0; col < ct::kGridSize; ++col) {
      double lat = 0.0;
      double lon = 0.0;
      ct::pointLatLon(v.lat, v.lon, v.span, row, col, &lat, &lon);
      double px = 0.0;
      double py = 0.0;
      ct::latLonToTilePixel(lat, lon, v.zoom, &px, &py);
      ct::TileId tile;
      tile.z = v.zoom;
      // Longitude wraps: a view across the antimeridian has samples whose pixel
      // column runs off the east edge but whose tile is the low-numbered one.
      const int span = 1 << v.zoom;
      const int raw_x = static_cast<int>(std::floor(px / ct::kTilePx));
      tile.x = ((raw_x % span) + span) % span;
      tile.y = static_cast<int>(std::floor(py / ct::kTilePx));

      const int16_t got = g->elev_m[row * ct::kGridSize + col];
      snprintf(msg, sizeof(msg), "grid sample row %d col %d", row, col);
      TEST_ASSERT_MESSAGE(got != 0, msg);
      TEST_ASSERT_EQUAL_INT16_MESSAGE(elevForTile(tile), got, msg);
    }
  }
}

}  // namespace

// --- Platform stubs (this env links no platform/native sources) --------------

namespace core::platform {

unsigned long nowMs() { return s_fake_now_ms; }

void logf(const char*, ...) {}

bool HttpClient::get(const char* url, BodyFn on_body,
                     unsigned long /*timeout_ms*/, PollFn poll) {
  s_urls.emplace_back(url);
  if (poll != nullptr) {
    poll();
  }

  if (!s_outcomes.empty()) {
    const bool ok = s_outcomes.front();
    s_outcomes.pop_front();
    if (!ok) {
      return false;
    }
  }

  // The body IS the URL: the injected decoder has nothing else to go on, and
  // this is what lets it answer with the tile that was actually requested.
  const std::string& body = s_urls.back();
  MemoryBodyReader reader(body.data(), body.size());
  return on_body(reader);
}

}  // namespace core::platform

// --- One request per tile, in tilesForView() order ---------------------------

void test_one_request_per_tile_in_view_order(void) {
  View v;
  TEST_ASSERT_TRUE_MESSAGE(findView(ct::kMaxTiles, kSpan, &v),
                           "no 4-tile view found");
  TEST_ASSERT_EQUAL_UINT(ct::kMaxTiles, v.tile_count);

  for (size_t i = 0; i < v.tile_count; ++i) {
    const bool done = ct::ensureGrid(v.lat, v.lon, kRange, v.span);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(i) + 1,
                          static_cast<int>(s_urls.size()));
    TEST_ASSERT_EQUAL_INT(i + 1 == v.tile_count ? 1 : 0, done ? 1 : 0);
    advanceMs(config::kTerrainTileIntervalMs);
  }

  for (size_t i = 0; i < v.tile_count; ++i) {
    TEST_ASSERT_EQUAL_STRING(urlOf(v.tiles[i]).c_str(), s_urls[i].c_str());
  }
  TEST_ASSERT_TRUE(ct::gridReady(v.lat, v.lon, kRange));
}

// --- Decoded tiles land in the grid ------------------------------------------

void test_single_tile_view_fills_the_whole_grid(void) {
  View v;
  TEST_ASSERT_TRUE_MESSAGE(findView(1, kSpan, &v), "no 1-tile view found");
  TEST_ASSERT_EQUAL_UINT(1, v.tile_count);

  TEST_ASSERT_TRUE(runToCompletion(v, kRange));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s_urls.size()));
  assertGridMatchesTiles(v);
}

void test_four_tile_view_fills_the_whole_grid(void) {
  View v;
  TEST_ASSERT_TRUE_MESSAGE(findView(ct::kMaxTiles, kSpan, &v),
                           "no 4-tile view found");
  TEST_ASSERT_EQUAL_UINT(ct::kMaxTiles, v.tile_count);

  TEST_ASSERT_TRUE(runToCompletion(v, kRange));
  TEST_ASSERT_EQUAL_INT(ct::kMaxTiles, static_cast<int>(s_urls.size()));
  assertGridMatchesTiles(v);
}

void test_view_across_the_antimeridian_fills_the_whole_grid(void) {
  // Tile x wraps at the antimeridian while the grid's pixel columns keep
  // running east past the edge of the world, so the two share no origin. Left
  // unhandled the download still completes and reports ready, with the columns
  // in the wrapped tile stuck at sea level — a hard false shoreline across the
  // disc rather than a visible failure.
  const View v = makeView(0.0, 179.8, kSpan);

  const int span = 1 << v.zoom;
  bool west_edge = false;
  bool east_edge = false;
  for (size_t i = 0; i < v.tile_count; ++i) {
    west_edge = west_edge || v.tiles[i].x == 0;
    east_edge = east_edge || v.tiles[i].x == span - 1;
  }
  TEST_ASSERT_TRUE_MESSAGE(west_edge && east_edge,
                           "view does not straddle the antimeridian");

  TEST_ASSERT_TRUE(runToCompletion(v, kRange));
  assertGridMatchesTiles(v);
}

// --- Request spacing ---------------------------------------------------------

void test_requests_are_spaced_by_the_tile_interval(void) {
  View v;
  TEST_ASSERT_TRUE_MESSAGE(findView(ct::kMaxTiles, kSpan, &v),
                           "no 4-tile view found");

  // The first tile of a fresh download is not spacing-gated.
  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s_urls.size()));

  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  }
  advanceMs(config::kTerrainTileIntervalMs - 1);
  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s_urls.size()));

  advanceMs(1);
  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(s_urls.size()));
}

// --- Failure handling --------------------------------------------------------

void test_failed_tile_is_retried_without_losing_progress(void) {
  View v;
  TEST_ASSERT_TRUE_MESSAGE(findView(ct::kMaxTiles, kSpan, &v),
                           "no 4-tile view found");

  // Second request fails once; everything else succeeds.
  s_outcomes = {true, false};

  TEST_ASSERT_TRUE(runToCompletion(v, kRange));
  TEST_ASSERT_EQUAL_INT(ct::kMaxTiles + 1, static_cast<int>(s_urls.size()));

  // The retry re-requested the same tile instead of restarting at the first.
  TEST_ASSERT_EQUAL_STRING(urlOf(v.tiles[1]).c_str(), s_urls[1].c_str());
  TEST_ASSERT_EQUAL_STRING(urlOf(v.tiles[1]).c_str(), s_urls[2].c_str());
  TEST_ASSERT_EQUAL_STRING(urlOf(v.tiles[2]).c_str(), s_urls[3].c_str());

  // Tile 0 was requested once, before the failure: the grid can only be whole
  // if what it decoded then survived the failed tile.
  assertGridMatchesTiles(v);
}

void test_download_is_abandoned_after_repeated_failures(void) {
  View v;
  TEST_ASSERT_TRUE_MESSAGE(findView(ct::kMaxTiles, kSpan, &v),
                           "no 4-tile view found");

  // First tile succeeds, then the second fails its three attempts.
  s_outcomes = {true, false, false, false};
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
    advanceMs(config::kTerrainTileIntervalMs);
  }
  TEST_ASSERT_EQUAL_INT(4, static_cast<int>(s_urls.size()));
  TEST_ASSERT_EQUAL_STRING(urlOf(v.tiles[1]).c_str(), s_urls[3].c_str());

  // Inside the retry gate: no more requests go out.
  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  advanceMs(config::kTerrainTileIntervalMs);
  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(4, static_cast<int>(s_urls.size()));

  // Once the gate expires the download restarts from the first tile.
  advanceMs(config::kTerrainRetryIntervalMs);
  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(5, static_cast<int>(s_urls.size()));
  TEST_ASSERT_EQUAL_STRING(urlOf(v.tiles[0]).c_str(), s_urls[4].c_str());
}

// --- View changes and caching -----------------------------------------------

void test_view_change_mid_download_restarts_it(void) {
  View a;
  TEST_ASSERT_TRUE_MESSAGE(findView(ct::kMaxTiles, kSpan, &a),
                           "no 4-tile view found");

  TEST_ASSERT_FALSE(ct::ensureGrid(a.lat, a.lon, kRange, a.span));
  advanceMs(config::kTerrainTileIntervalMs);
  TEST_ASSERT_FALSE(ct::ensureGrid(a.lat, a.lon, kRange, a.span));
  TEST_ASSERT_EQUAL_INT(2, static_cast<int>(s_urls.size()));

  // A different centre abandons the two decoded tiles and starts over —
  // immediately, since a fresh download is not spacing-gated.
  const View b = makeView(a.lat + 1.0, a.lon, a.span);
  TEST_ASSERT_TRUE(b.tile_count > 0);
  TEST_ASSERT_FALSE(ct::ensureGrid(b.lat, b.lon, kRange, b.span));
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(s_urls.size()));
  TEST_ASSERT_EQUAL_STRING(urlOf(b.tiles[0]).c_str(), s_urls[2].c_str());

  TEST_ASSERT_TRUE(runToCompletion(b, kRange));
  TEST_ASSERT_TRUE(ct::gridReady(b.lat, b.lon, kRange));
  TEST_ASSERT_FALSE(ct::gridReady(a.lat, a.lon, kRange));
  assertGridMatchesTiles(b);
}

void test_a_cached_grid_is_served_without_refetching(void) {
  const View v = makeView(kBaseLat, kBaseLon, kSpan);
  TEST_ASSERT_TRUE(runToCompletion(v, kRange));

  const size_t before = s_urls.size();
  TEST_ASSERT_TRUE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(before),
                        static_cast<int>(s_urls.size()));
}

void test_a_range_change_evicts_the_single_cached_grid(void) {
  const View tight = makeView(kBaseLat, kBaseLon, kSpan);
  const View wide = makeView(kBaseLat, kBaseLon, kSpan * 2.0f);

  TEST_ASSERT_TRUE(runToCompletion(tight, 0));
  TEST_ASSERT_TRUE(ct::gridReady(kBaseLat, kBaseLon, 0));

  // One slot, deliberately: four would cost 13 KB of static RAM, which on the
  // ESP32-C3 is the difference between a TLS session fitting and not.
  TEST_ASSERT_TRUE(runToCompletion(wide, 1));
  TEST_ASSERT_TRUE(ct::gridReady(kBaseLat, kBaseLon, 1));
  TEST_ASSERT_NOT_NULL(ct::grid(1));
  TEST_ASSERT_FALSE(ct::gridReady(kBaseLat, kBaseLon, 0));
  TEST_ASSERT_NULL(ct::grid(0));

  // Going back re-downloads rather than serving the evicted grid.
  const size_t before = s_urls.size();
  ct::ensureGrid(tight.lat, tight.lon, 0, tight.span);
  TEST_ASSERT_TRUE(s_urls.size() > before);
}

void test_the_retry_gate_belongs_to_the_view_that_failed(void) {
  const View v = makeView(kBaseLat, kBaseLon, kSpan);
  s_outcomes = {false, false, false};
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
    advanceMs(config::kTerrainTileIntervalMs);
  }
  const size_t gated = s_urls.size();

  // The same view waits out the gate.
  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(gated),
                        static_cast<int>(s_urls.size()));

  // Another preset must not inherit that wait: with one cache slot, tapping to
  // a different range is exactly how a user asks for another attempt.
  const View wide = makeView(kBaseLat, kBaseLon, kSpan * 2.0f);
  ct::ensureGrid(wide.lat, wide.lon, kRange + 1, wide.span);
  TEST_ASSERT_TRUE(s_urls.size() > gated);
}

// --- One decode per tile ----------------------------------------------------

void test_each_tile_is_decoded_exactly_once(void) {
  View v;
  TEST_ASSERT_TRUE_MESSAGE(findView(ct::kMaxTiles, kSpan, &v),
                           "no 4-tile view found");
  TEST_ASSERT_TRUE(runToCompletion(v, kRange));
  TEST_ASSERT_EQUAL_INT(ct::kMaxTiles, s_decodes);

  // A cached view decodes nothing further.
  TEST_ASSERT_TRUE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(ct::kMaxTiles, s_decodes);
}

void test_a_request_that_never_delivers_a_body_decodes_nothing(void) {
  const View v = makeView(kBaseLat, kBaseLon, kSpan);
  s_outcomes = {false};

  TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s_urls.size()));
  TEST_ASSERT_EQUAL_INT(0, s_decodes);
}

// --- The decoder seam is mandatory ------------------------------------------

void test_no_decoder_means_no_download(void) {
  ct::setPngDecoder(nullptr);

  const View v = makeView(kBaseLat, kBaseLon, kSpan);
  for (int i = 0; i < 3; ++i) {
    TEST_ASSERT_FALSE(ct::ensureGrid(v.lat, v.lon, kRange, v.span));
    advanceMs(config::kTerrainTileIntervalMs);
  }
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(s_urls.size()));
  TEST_ASSERT_FALSE(ct::gridReady(v.lat, v.lon, kRange));
}

void setUp(void) {
  ct::clear();
  ct::setPngDecoder(fakeDecode);
  s_urls.clear();
  s_outcomes.clear();
  s_fake_now_ms = 0;
  s_decodes = 0;
}

void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_one_request_per_tile_in_view_order);

  RUN_TEST(test_single_tile_view_fills_the_whole_grid);
  RUN_TEST(test_four_tile_view_fills_the_whole_grid);
  RUN_TEST(test_view_across_the_antimeridian_fills_the_whole_grid);

  RUN_TEST(test_requests_are_spaced_by_the_tile_interval);

  RUN_TEST(test_failed_tile_is_retried_without_losing_progress);
  RUN_TEST(test_download_is_abandoned_after_repeated_failures);

  RUN_TEST(test_view_change_mid_download_restarts_it);
  RUN_TEST(test_a_cached_grid_is_served_without_refetching);
  RUN_TEST(test_a_range_change_evicts_the_single_cached_grid);
  RUN_TEST(test_the_retry_gate_belongs_to_the_view_that_failed);

  RUN_TEST(test_each_tile_is_decoded_exactly_once);
  RUN_TEST(test_a_request_that_never_delivers_a_body_decodes_nothing);

  RUN_TEST(test_no_decoder_means_no_download);

  return UNITY_END();
}
