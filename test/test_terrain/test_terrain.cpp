/**
 * Host unit tests for the pure helpers in core::terrain.
 *
 * These lock the parts of the terrain layer that break silently: the grid point
 * projection the overlay depends on (row 0 = north, col 0 = west, flat-earth
 * 111 km/deg like core::geo), the Web Mercator projection and tile selection
 * the terrain-RGB download is built on, the terrarium pixel decoder, and the
 * hypsometric band lookup.
 *
 * The Mercator numbers below are literals computed off-line from the standard
 * slippy-map formula (px = (lon+180)/360 * 256*2^z, py = (1 - ln(tan(lat) +
 * sec(lat))/pi)/2 * 256*2^z), not from our own code — an inverted axis or a
 * wrong scale in latLonToTilePixel() therefore cannot agree with the test that
 * checks it.
 *
 * Not covered here: ensureGrid(), gridReady(), the per-preset grid cache and
 * anything else that touches HTTP or the PNG decoder. test/test_terrain_fetch
 * drives that state machine with a scripted client and a hand-cranked clock.
 */

#include <unity.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "core/settings.h"
#include "core/terrain.h"

namespace ct = core::terrain;

namespace {

constexpr double kLat = 47.0753;
constexpr double kLon = 15.4062;

/**
 * How the real caller sizes a view: ui::radar::terrainHalfSpanKm() scales the
 * preset's outer-ring distance out to the screen edge (screen centre at 120 px
 * against the 107 px outer ring). Reproduced as a number so the test stays
 * clear of ui/, which would drag the display headers in.
 */
constexpr float kScreenEdgeScale = 120.0f / 107.0f;

constexpr float halfSpanKm(size_t preset) {
  return core::settings::kRangePresets[preset].outer_km * kScreenEdgeScale;
}

/** The 20 NM default preset: ~55.4 km from the centre to the screen edge. */
constexpr float kHalfSpanKm = halfSpanKm(1);

constexpr int kCenter = ct::kGridSize / 2;  // exact centre for an odd grid
constexpr double kDegPerHalfSpan = kHalfSpanKm / 111.0;

/** Pixel bounds of the grid, over every sample rather than just the corners. */
struct PixelBox {
  double px_min;
  double px_max;
  double py_min;
  double py_max;
};

PixelBox gridPixelBox(double lat, double lon, float half_span_km, int zoom) {
  PixelBox box = {1e30, -1e30, 1e30, -1e30};
  for (int row = 0; row < ct::kGridSize; ++row) {
    for (int col = 0; col < ct::kGridSize; ++col) {
      double p_lat = 0.0;
      double p_lon = 0.0;
      double px = 0.0;
      double py = 0.0;
      ct::pointLatLon(lat, lon, half_span_km, row, col, &p_lat, &p_lon);
      ct::latLonToTilePixel(p_lat, p_lon, zoom, &px, &py);
      if (px < box.px_min) box.px_min = px;
      if (px > box.px_max) box.px_max = px;
      if (py < box.py_min) box.py_min = py;
      if (py > box.py_max) box.py_max = py;
    }
  }
  return box;
}

/** Tile a pixel position lands in, with the x wrap tilesForView() applies. */
ct::TileId tileOfPixel(double px, double py, int zoom) {
  const int span = 1 << zoom;
  const int raw_x = static_cast<int>(std::floor(px / ct::kTilePx));
  ct::TileId t;
  t.z = zoom;
  t.x = ((raw_x % span) + span) % span;
  t.y = static_cast<int>(std::floor(py / ct::kTilePx));
  return t;
}

bool sameTile(const ct::TileId& a, const ct::TileId& b) {
  return a.z == b.z && a.x == b.x && a.y == b.y;
}

/**
 * Asserts the tile set for a view is usable AND that every grid sample lands
 * inside it, then returns the tile count.
 *
 * The coverage half is the point: a sample no requested tile contains is never
 * written, so it keeps the zero the grid was cleared to and the overlay paints
 * that patch as sea level — a silent hole, not a visible failure.
 */
size_t assertViewIsCovered(double lat, double lon, float half_span_km) {
  const int zoom = ct::zoomForView(lat, lon, half_span_km);
  ct::TileId tiles[ct::kMaxTiles];
  const size_t count =
      ct::tilesForView(lat, lon, half_span_km, zoom, tiles, ct::kMaxTiles);

  TEST_ASSERT_GREATER_THAN_UINT32(0, static_cast<uint32_t>(count));
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(static_cast<uint32_t>(ct::kMaxTiles),
                                   static_cast<uint32_t>(count));
  // A bounding box crossing at most one boundary per axis touches a 1x1, 1x2,
  // 2x1 or 2x2 block, so three tiles would mean a dropped corner.
  TEST_ASSERT_NOT_EQUAL_MESSAGE(3, count, "tile block is never 3 wide");

  const int span = 1 << zoom;
  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(zoom, tiles[i].z, "tile carries its zoom");
    TEST_ASSERT_TRUE_MESSAGE(tiles[i].x >= 0 && tiles[i].x < span,
                             "tile x outside the projection");
    TEST_ASSERT_TRUE_MESSAGE(tiles[i].y >= 0 && tiles[i].y < span,
                             "tile y outside the projection");
    for (size_t j = 0; j < i; ++j) {
      TEST_ASSERT_FALSE_MESSAGE(sameTile(tiles[i], tiles[j]),
                                "same tile requested twice");
    }
  }

  for (int row = 0; row < ct::kGridSize; ++row) {
    for (int col = 0; col < ct::kGridSize; ++col) {
      double p_lat = 0.0;
      double p_lon = 0.0;
      double px = 0.0;
      double py = 0.0;
      ct::pointLatLon(lat, lon, half_span_km, row, col, &p_lat, &p_lon);
      ct::latLonToTilePixel(p_lat, p_lon, zoom, &px, &py);
      const ct::TileId want = tileOfPixel(px, py, zoom);
      bool found = false;
      for (size_t i = 0; i < count && !found; ++i) {
        found = sameTile(tiles[i], want);
      }
      TEST_ASSERT_TRUE_MESSAGE(found, "grid sample has no tile to read from");
    }
  }
  return count;
}

/**
 * First centre in a 30..50 N / 10 W..10 E sweep whose view needs exactly
 * `want` tiles. Searched rather than hard-coded because which side of a tile
 * boundary a view falls on is an artefact of the projection, not something a
 * reader could pick by hand.
 */
bool findCentreForTileCount(size_t want, float half_span_km, double* lat,
                            double* lon) {
  for (int i = 0; i < 41; ++i) {
    for (int j = 0; j < 41; ++j) {
      const double try_lat = 30.0 + 0.5 * i;
      const double try_lon = -10.0 + 0.5 * j;
      const int zoom = ct::zoomForView(try_lat, try_lon, half_span_km);
      ct::TileId tiles[ct::kMaxTiles];
      const size_t count = ct::tilesForView(try_lat, try_lon, half_span_km,
                                            zoom, tiles, ct::kMaxTiles);
      if (count == want) {
        *lat = try_lat;
        *lon = try_lon;
        return true;
      }
    }
  }
  return false;
}

}  // namespace

// --- pointLatLon ---------------------------------------------------------------

void test_pointLatLon_centre_is_exact(void) {
  double lat = 0.0;
  double lon = 0.0;
  ct::pointLatLon(kLat, kLon, kHalfSpanKm, kCenter, kCenter, &lat, &lon);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, kLat, lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, kLon, lon);
}

void test_pointLatLon_row0_north_col0_west(void) {
  double lat = 0.0;
  double lon = 0.0;
  ct::pointLatLon(kLat, kLon, kHalfSpanKm, 0, kCenter, &lat, &lon);
  TEST_ASSERT_TRUE_MESSAGE(lat > kLat, "row 0 must be north of the centre");
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, kLat + kDegPerHalfSpan, lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, kLon, lon);

  ct::pointLatLon(kLat, kLon, kHalfSpanKm, kCenter, 0, &lat, &lon);
  TEST_ASSERT_TRUE_MESSAGE(lon < kLon, "col 0 must be west of the centre");
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, kLon - kDegPerHalfSpan, lon);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, kLat, lat);
}

void test_pointLatLon_corners_are_symmetric(void) {
  const int last = ct::kGridSize - 1;
  double nw_lat, nw_lon, ne_lat, ne_lon, sw_lat, sw_lon, se_lat, se_lon;
  ct::pointLatLon(kLat, kLon, kHalfSpanKm, 0, 0, &nw_lat, &nw_lon);
  ct::pointLatLon(kLat, kLon, kHalfSpanKm, 0, last, &ne_lat, &ne_lon);
  ct::pointLatLon(kLat, kLon, kHalfSpanKm, last, 0, &sw_lat, &sw_lon);
  ct::pointLatLon(kLat, kLon, kHalfSpanKm, last, last, &se_lat, &se_lon);

  // North offset of row 0 mirrors the south offset of the last row, and the
  // west offset of col 0 mirrors the east offset of the last col.
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, nw_lat - kLat, kLat - sw_lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, kLon - nw_lon, ne_lon - kLon);

  // Corners on the same row/col line up exactly.
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, nw_lat, ne_lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, sw_lat, se_lat);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, nw_lon, sw_lon);
  TEST_ASSERT_DOUBLE_WITHIN(1e-12, ne_lon, se_lon);
}

void test_pointLatLon_is_linear(void) {
  // Halfway between the north edge and the centre sits at half the offset.
  double lat = 0.0;
  double lon = 0.0;
  ct::pointLatLon(kLat, kLon, kHalfSpanKm, kCenter / 2, kCenter, &lat, &lon);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, kLat + kDegPerHalfSpan / 2.0, lat);
}

// --- terrariumElevation --------------------------------------------------------

void test_terrarium_sea_level_is_zero(void) {
  // The encoding's -32768 m bias makes R=128 the zero point; ocean pixels are
  // the overwhelming majority of what we decode, and an off-by-one bias here
  // would tint every coastline.
  TEST_ASSERT_EQUAL_INT16(0, ct::terrariumElevation(128, 0, 0));
}

void test_terrarium_whole_metres(void) {
  // R is the 256 m digit, G the 1 m digit: 162*256 + 145 - 32768 = 8849, the
  // height of Everest, and the tallest value real terrain data contains.
  TEST_ASSERT_EQUAL_INT16(8849, ct::terrariumElevation(162, 145, 0));
  TEST_ASSERT_EQUAL_INT16(256, ct::terrariumElevation(129, 0, 0));

  // Terrarium tiles carry bathymetry too, so negatives are ordinary data and
  // must not clamp at zero.
  TEST_ASSERT_EQUAL_INT16(-1, ct::terrariumElevation(127, 255, 0));
  TEST_ASSERT_EQUAL_INT16(-4000, ct::terrariumElevation(112, 96, 0));
}

void test_terrarium_blue_is_sub_metre(void) {
  // B is the 1/256 m digit. It only ever shifts the result by rounding, which
  // is what distinguishes a correct decode from one that drops the channel.
  TEST_ASSERT_EQUAL_INT16(0, ct::terrariumElevation(128, 0, 64));    // +0.25 m
  TEST_ASSERT_EQUAL_INT16(1, ct::terrariumElevation(128, 0, 192));   // +0.75 m
  TEST_ASSERT_EQUAL_INT16(11, ct::terrariumElevation(128, 10, 192));

  // -0.75 m: rounding to nearest gives -1, truncation towards zero would give
  // 0 and quietly lift sub-metre water above the first hypsometric band.
  TEST_ASSERT_EQUAL_INT16(-1, ct::terrariumElevation(127, 255, 64));
}

void test_terrarium_extremes_of_the_encoding(void) {
  // The floor of the encoding is exactly INT16_MIN, so it survives the return
  // type intact.
  TEST_ASSERT_EQUAL_INT16(-32768, ct::terrariumElevation(0, 0, 0));

  // The largest height that still fits the return type, INT16_MAX, is a real
  // pixel value.
  TEST_ASSERT_EQUAL_INT16(32767, ct::terrariumElevation(255, 255, 127));

  // The encoding's ceiling is 32767.996 m, which rounds one past INT16_MAX, so
  // the last half-pixel of the range (R=G=255, B>=128) saturates. No terrain
  // pixel gets near it — Everest encodes as R=162 and the deepest bathymetry as
  // R=85 — so this only ever fires on a corrupt payload, where a peak must not
  // wrap into a -32768 m hole.
  TEST_ASSERT_EQUAL_INT16(32767, ct::terrariumElevation(255, 255, 255));
}

// --- latLonToTilePixel ---------------------------------------------------------

void test_tilePixel_zoom0_centre_of_the_world(void) {
  double px = 0.0;
  double py = 0.0;
  // Standard slippy-map formula: null island sits dead centre of the single
  // zoom 0 tile.
  ct::latLonToTilePixel(0.0, 0.0, 0, &px, &py);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 128.0, px);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 128.0, py);

  // Longitude maps linearly onto the whole width, antimeridian to antimeridian.
  ct::latLonToTilePixel(0.0, -180.0, 0, &px, &py);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, px);
  ct::latLonToTilePixel(0.0, 180.0, 0, &px, &py);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 256.0, px);
}

void test_tilePixel_reference_points(void) {
  double px = 0.0;
  double py = 0.0;

  // San Francisco (37.7749, -122.4194) at zoom 12, from the standard formula:
  // px = (lon+180)/360 * 256*2^12, py = (1 - ln(tan+sec)/pi)/2 * 256*2^12.
  ct::latLonToTilePixel(37.7749, -122.4194, 12, &px, &py);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 167715.65340444446, px);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 405296.57774392294, py);

  // Sydney (-33.8688, 151.2093) at zoom 14, same formula. A southern, eastern
  // point catches a sign flip that a single northern reference would not.
  ct::latLonToTilePixel(-33.8688, 151.2093, 14, &px, &py);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 3858868.0328533333, px);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 2516969.330662551, py);

  // The radar's own default centre, Graz, at zoom 12.
  ct::latLonToTilePixel(kLat, kLon, 12, &px, &py);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 569161.80992, px);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 368489.85464324185, py);
}

void test_tilePixel_north_is_up_and_zoom_doubles(void) {
  double px_lo = 0.0;
  double py_lo = 0.0;
  double px_hi = 0.0;
  double py_hi = 0.0;

  // Mercator y grows southward; the resampler's reverse map assumes it, since
  // it walks grid rows expecting non-decreasing py.
  ct::latLonToTilePixel(48.0, 15.0, 10, &px_lo, &py_lo);
  ct::latLonToTilePixel(46.0, 15.0, 10, &px_hi, &py_hi);
  TEST_ASSERT_TRUE_MESSAGE(py_hi > py_lo,
                           "a southern point must have a bigger py");

  // One zoom level up is exactly twice the pixel space, which is what lets
  // zoomForView() halve the view's pixel span per step.
  ct::latLonToTilePixel(46.0, 15.0, 11, &px_lo, &py_lo);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0 * px_hi, px_lo);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 2.0 * py_hi, py_lo);
}

void test_tilePixel_clamps_at_the_mercator_limit(void) {
  double px_pole = 0.0;
  double py_pole = 0.0;
  double px_limit = 0.0;
  double py_limit = 0.0;

  // py diverges at the poles, so a radar centre in the high Arctic must fold
  // onto the +/-85.051129 cut-off instead of producing an infinity that would
  // poison the tile arithmetic.
  ct::latLonToTilePixel(90.0, 0.0, 0, &px_pole, &py_pole);
  ct::latLonToTilePixel(85.05112878, 0.0, 0, &px_limit, &py_limit);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, py_limit, py_pole);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 0.0, py_pole);  // top edge of the world

  ct::latLonToTilePixel(-90.0, 0.0, 0, &px_pole, &py_pole);
  ct::latLonToTilePixel(-85.05112878, 0.0, 0, &px_limit, &py_limit);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, py_limit, py_pole);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 256.0, py_pole);  // bottom edge
}

// --- zoomForView ---------------------------------------------------------------

void test_zoom_is_maximal_for_every_range_preset(void) {
  // The whole point of zoomForView() is "as much detail as 4 tiles can buy":
  // the view must fit 256 px on both axes, and one level finer must not.
  for (size_t p = 0; p < core::settings::kRangePresetCount; ++p) {
    const float span_km = halfSpanKm(p);
    const int zoom = ct::zoomForView(kLat, kLon, span_km);
    TEST_ASSERT_TRUE_MESSAGE(zoom >= 0 && zoom <= ct::kMaxZoom,
                             "zoom outside what the tile source publishes");

    const PixelBox fits = gridPixelBox(kLat, kLon, span_km, zoom);
    TEST_ASSERT_TRUE_MESSAGE(fits.px_max - fits.px_min <= ct::kTilePx,
                             "view is wider than a tile at the chosen zoom");
    TEST_ASSERT_TRUE_MESSAGE(fits.py_max - fits.py_min <= ct::kTilePx,
                             "view is taller than a tile at the chosen zoom");

    if (zoom == 0 || zoom == ct::kMaxZoom) {
      continue;  // no finer level to compare against
    }
    const PixelBox finer = gridPixelBox(kLat, kLon, span_km, zoom + 1);
    const bool too_big = finer.px_max - finer.px_min > ct::kTilePx ||
                         finer.py_max - finer.py_min > ct::kTilePx;
    TEST_ASSERT_TRUE_MESSAGE(too_big, "a finer zoom would also have fitted");
  }
}

void test_zoom_matches_the_standard_formula(void) {
  // Independently: the widest axis of the box must stay within 256 px, so the
  // limit is 2^z <= 360 / span_deg / sec(lat) at 111 km/deg. For the four
  // presets at the default centre that gives 8, 7, 6, 5 — one level per
  // doubling of range, as the presets themselves double.
  const int expected[] = {8, 7, 6, 5};
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      core::settings::kRangePresetCount,
      sizeof(expected) / sizeof(expected[0]),
      "a range preset was added without an expected zoom");
  for (size_t p = 0; p < core::settings::kRangePresetCount; ++p) {
    TEST_ASSERT_EQUAL_INT(expected[p], ct::zoomForView(kLat, kLon,
                                                       halfSpanKm(p)));
  }
}

void test_zoom_never_rises_as_the_view_widens(void) {
  int previous = ct::kMaxZoom + 1;
  for (size_t p = 0; p < core::settings::kRangePresetCount; ++p) {
    const int zoom = ct::zoomForView(kLat, kLon, halfSpanKm(p));
    TEST_ASSERT_TRUE_MESSAGE(zoom <= previous,
                             "a wider range preset asked for finer tiles");
    previous = zoom;
  }
}

void test_zoom_drops_towards_the_poles(void) {
  // Mercator stretches y by sec(lat), so the same ground span needs coarser
  // tiles further north. Picking the zoom off longitude alone would look fine
  // at the equator and overflow the 2x2 block in Scandinavia.
  const int equator = ct::zoomForView(0.0, kLon, kHalfSpanKm);
  const int mid = ct::zoomForView(kLat, kLon, kHalfSpanKm);
  const int high = ct::zoomForView(70.0, kLon, kHalfSpanKm);
  TEST_ASSERT_TRUE_MESSAGE(mid <= equator, "mid latitude needs coarser tiles");
  TEST_ASSERT_TRUE_MESSAGE(high < equator, "high latitude needs coarser tiles");
}

// --- tilesForView --------------------------------------------------------------

void test_tiles_cover_every_grid_sample(void) {
  // Run the coverage property over the real presets and over centres that
  // stress the projection: the equator, the southern hemisphere, and either
  // side of the antimeridian where the tile x index wraps.
  for (size_t p = 0; p < core::settings::kRangePresetCount; ++p) {
    assertViewIsCovered(kLat, kLon, halfSpanKm(p));
    assertViewIsCovered(0.0, 0.0, halfSpanKm(p));
    assertViewIsCovered(-33.8688, 151.2093, halfSpanKm(p));
    assertViewIsCovered(64.0, -179.9, halfSpanKm(p));
    assertViewIsCovered(-41.0, 179.9, halfSpanKm(p));
  }
}

void test_tiles_single_tile_view_is_covered(void) {
  // The cheapest case, one request for the whole grid.
  double lat = 0.0;
  double lon = 0.0;
  TEST_ASSERT_TRUE_MESSAGE(findCentreForTileCount(1, kHalfSpanKm, &lat, &lon),
                           "no single-tile centre in the search window");
  TEST_ASSERT_EQUAL_UINT32(1, assertViewIsCovered(lat, lon, kHalfSpanKm));
}

void test_tiles_straddling_one_boundary_is_covered(void) {
  // Two tiles: the box crosses a boundary on exactly one axis, so the samples
  // on the far side of it come from the second request.
  double lat = 0.0;
  double lon = 0.0;
  TEST_ASSERT_TRUE_MESSAGE(findCentreForTileCount(2, kHalfSpanKm, &lat, &lon),
                           "no two-tile centre in the search window");
  TEST_ASSERT_EQUAL_UINT32(2, assertViewIsCovered(lat, lon, kHalfSpanKm));
}

void test_tiles_straddling_a_corner_is_covered(void) {
  // The worst case the design allows: a boundary on both axes, all four tiles,
  // and the one place where a missing corner tile would leave a quadrant of
  // the screen flat.
  double lat = 0.0;
  double lon = 0.0;
  TEST_ASSERT_TRUE_MESSAGE(findCentreForTileCount(4, kHalfSpanKm, &lat, &lon),
                           "no four-tile centre in the search window");
  TEST_ASSERT_EQUAL_UINT32(4, assertViewIsCovered(lat, lon, kHalfSpanKm));
}

void test_tiles_respect_the_output_capacity(void) {
  // ensureGrid() sizes its array at kMaxTiles, so a view that touches more
  // must be truncated rather than written past the end.
  double lat = 0.0;
  double lon = 0.0;
  TEST_ASSERT_TRUE(findCentreForTileCount(4, kHalfSpanKm, &lat, &lon));
  const int zoom = ct::zoomForView(lat, lon, kHalfSpanKm);

  ct::TileId one[1];
  TEST_ASSERT_EQUAL_UINT32(
      1, ct::tilesForView(lat, lon, kHalfSpanKm, zoom, one, 1));

  ct::TileId some[ct::kMaxTiles];
  TEST_ASSERT_EQUAL_UINT32(
      0, ct::tilesForView(lat, lon, kHalfSpanKm, zoom, some, 0));
  TEST_ASSERT_EQUAL_UINT32(
      0, ct::tilesForView(lat, lon, kHalfSpanKm, zoom, nullptr, ct::kMaxTiles));
}

// --- buildTileUrl --------------------------------------------------------------

void test_buildTileUrl_substitutes_z_x_y_in_order(void) {
  char url[160];
  ct::buildTileUrl(url, sizeof(url), ct::TileId{10, 544, 354});

  // Distinct numbers on purpose: /z/x/y and /z/y/x both look plausible, and
  // swapping them fetches real tiles from the wrong part of the world.
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(url, "/10/544/354.png"),
                               "tile path is not z/x/y");

  // Everything before the first format specifier is the source's fixed prefix;
  // comparing against config avoids restating the bucket URL here.
  const char* fmt = config::kTerrainTileUrlFmt;
  const size_t prefix = static_cast<size_t>(strchr(fmt, '%') - fmt);
  TEST_ASSERT_EQUAL_MEMORY(fmt, url, prefix);
}

void test_buildTileUrl_truncates_instead_of_overflowing(void) {
  // The device builds this into a fixed 160-byte buffer; a longer source URL
  // must clip and stay terminated rather than run off the stack.
  char url[12];
  memset(url, 'x', sizeof(url));
  ct::buildTileUrl(url, sizeof(url), ct::TileId{15, 32767, 32767});
  TEST_ASSERT_TRUE_MESSAGE(strlen(url) < sizeof(url), "URL was not terminated");
}

// --- bandForElevation ------------------------------------------------------------

static const int16_t kBandFloors[] = {1, 200, 500, 1000, 1500, 2000, 3000};
static const int kBandCount =
    static_cast<int>(sizeof(kBandFloors) / sizeof(kBandFloors[0]));

void test_band_water_below_first_floor(void) {
  TEST_ASSERT_EQUAL_INT(-1, ct::bandForElevation(0, kBandFloors, kBandCount));
  TEST_ASSERT_EQUAL_INT(-1, ct::bandForElevation(-10, kBandFloors, kBandCount));
}

void test_band_lowest(void) {
  TEST_ASSERT_EQUAL_INT(0, ct::bandForElevation(1, kBandFloors, kBandCount));
  TEST_ASSERT_EQUAL_INT(0, ct::bandForElevation(199, kBandFloors, kBandCount));
}

void test_band_boundary_lands_in_higher_band(void) {
  TEST_ASSERT_EQUAL_INT(1, ct::bandForElevation(200, kBandFloors, kBandCount));
  TEST_ASSERT_EQUAL_INT(1, ct::bandForElevation(499, kBandFloors, kBandCount));
  TEST_ASSERT_EQUAL_INT(2, ct::bandForElevation(500, kBandFloors, kBandCount));
  TEST_ASSERT_EQUAL_INT(3, ct::bandForElevation(1000, kBandFloors, kBandCount));
  TEST_ASSERT_EQUAL_INT(5, ct::bandForElevation(2999, kBandFloors, kBandCount));
}

void test_band_top_is_open_ended(void) {
  TEST_ASSERT_EQUAL_INT(6, ct::bandForElevation(3000, kBandFloors, kBandCount));
  TEST_ASSERT_EQUAL_INT(6, ct::bandForElevation(9000, kBandFloors, kBandCount));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_pointLatLon_centre_is_exact);
  RUN_TEST(test_pointLatLon_row0_north_col0_west);
  RUN_TEST(test_pointLatLon_corners_are_symmetric);
  RUN_TEST(test_pointLatLon_is_linear);

  RUN_TEST(test_terrarium_sea_level_is_zero);
  RUN_TEST(test_terrarium_whole_metres);
  RUN_TEST(test_terrarium_blue_is_sub_metre);
  RUN_TEST(test_terrarium_extremes_of_the_encoding);

  RUN_TEST(test_tilePixel_zoom0_centre_of_the_world);
  RUN_TEST(test_tilePixel_reference_points);
  RUN_TEST(test_tilePixel_north_is_up_and_zoom_doubles);
  RUN_TEST(test_tilePixel_clamps_at_the_mercator_limit);

  RUN_TEST(test_zoom_is_maximal_for_every_range_preset);
  RUN_TEST(test_zoom_matches_the_standard_formula);
  RUN_TEST(test_zoom_never_rises_as_the_view_widens);
  RUN_TEST(test_zoom_drops_towards_the_poles);

  RUN_TEST(test_tiles_cover_every_grid_sample);
  RUN_TEST(test_tiles_single_tile_view_is_covered);
  RUN_TEST(test_tiles_straddling_one_boundary_is_covered);
  RUN_TEST(test_tiles_straddling_a_corner_is_covered);
  RUN_TEST(test_tiles_respect_the_output_capacity);

  RUN_TEST(test_buildTileUrl_substitutes_z_x_y_in_order);
  RUN_TEST(test_buildTileUrl_truncates_instead_of_overflowing);

  RUN_TEST(test_band_water_below_first_floor);
  RUN_TEST(test_band_lowest);
  RUN_TEST(test_band_boundary_lands_in_higher_band);
  RUN_TEST(test_band_top_is_open_ended);

  return UNITY_END();
}
