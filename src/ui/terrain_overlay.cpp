/**
 * Terrain elevation background: bilinearly upsample the cached elevation
 * grid (config::kTerrainGridSize per side) to the full 240x240 frame and
 * paint hypsometric green bands behind the radar grid.
 *
 * Every pixel of the frame passes through here, which on a core without an FPU
 * makes the arithmetic below the redraw's hot loop; see the note on kFracBits.
 */

#include "ui/terrain_overlay.h"

#include "core/settings.h"
#include "core/terrain.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::terrain {
namespace {

constexpr int kGrid = core::terrain::kGridSize;

/**
 * Interpolation weights are fixed-point, not float, and that is a performance
 * decision rather than a stylistic one: the ESP32-C3 core has no FPU, so every
 * float operation is a library call. Upsampling the grid touches 57,600 pixels
 * per frame, which in floats measured 209 ms of a 297 ms frame — the single
 * biggest cost in the whole redraw.
 *
 * 8 fractional bits put the weight error below 1/256 of a grid cell, i.e. far
 * under a pixel, and elevations are carried in whole metres: the bands are
 * hundreds of metres apart, so sub-metre precision would be measuring nothing.
 */
constexpr int kFracBits = 8;
constexpr int32_t kFracOne = 1 << kFracBits;

// Pixel -> grid-cell mapping is identical on both axes and for every frame
// (pixel 0 -> grid line 0, pixel kSize-1 -> grid line kGrid-1), so the cell
// index and fractional weight per pixel are computed once and reused.
int s_cell[radar::kSize];
int32_t s_frac[radar::kSize];  ///< 0..kFracOne
bool s_map_ready = false;

void initPixelToGridMap() {
  if (s_map_ready) {
    return;
  }
  for (int i = 0; i < radar::kSize; ++i) {
    const int32_t position = static_cast<int32_t>(i) * (kGrid - 1) * kFracOne /
                             (radar::kSize - 1);
    int cell = static_cast<int>(position >> kFracBits);
    int32_t frac = position & (kFracOne - 1);
    // The last pixel lands exactly on the final grid line; keep the cell index
    // one short so cell+1 stays in range, with the weight fully on it.
    if (cell > kGrid - 2) {
      cell = kGrid - 2;
      frac = kFracOne;
    }
    s_cell[i] = cell;
    s_frac[i] = frac;
  }
  s_map_ready = true;
}

int bandAtPixel(const int32_t* row_elev, int x) {
  const int c = s_cell[x];
  const int32_t west = row_elev[c];
  const int32_t elev_m =
      west + (((row_elev[c + 1] - west) * s_frac[x]) >> kFracBits);
  return core::terrain::bandForElevation(static_cast<int16_t>(elev_m),
                                         radar::kTerrainBandMinM,
                                         radar::kTerrainBandCount);
}

void drawScanline(lgfx::LGFXBase& gfx, const core::terrain::Grid& grid,
                  int y) {
  // The row weight is constant across the scanline: blend the two bracketing
  // grid rows into one kGrid-entry row up front so the per-pixel work is a
  // single horizontal lerp. Whole metres keep both lerps inside int32 for any
  // pair of terrarium values, so neither can overflow on real or absurd data.
  const int r = s_cell[y];
  const int32_t wy = s_frac[y];
  const int16_t* north = &grid.elev_m[r * kGrid];
  const int16_t* south = &grid.elev_m[(r + 1) * kGrid];
  int32_t row_elev[kGrid];
  for (int c = 0; c < kGrid; ++c) {
    const int32_t n = north[c];
    row_elev[c] = n + (((static_cast<int32_t>(south[c]) - n) * wy) >> kFracBits);
  }

  // Neighbouring pixels almost always fall in the same band, so runs are
  // coalesced into one drawFastHLine each instead of 240 drawPixel calls.
  // Band -1 (water / below the first floor) draws nothing: the plain
  // background fill underneath stays visible.
  int run_start = 0;
  int run_band = bandAtPixel(row_elev, 0);
  for (int x = 1; x < radar::kSize; ++x) {
    const int band = bandAtPixel(row_elev, x);
    if (band == run_band) {
      continue;
    }
    if (run_band >= 0) {
      gfx.drawFastHLine(run_start, y, x - run_start,
                        radar::kColorTerrain[run_band]);
    }
    run_start = x;
    run_band = band;
  }
  if (run_band >= 0) {
    gfx.drawFastHLine(run_start, y, radar::kSize - run_start,
                      radar::kColorTerrain[run_band]);
  }
}

}  // namespace

void drawTerrainBackground(lgfx::LGFXBase& gfx) {
  if (!radar::showTerrain()) {
    return;
  }
  const double lat = core::settings::lat();
  const double lon = core::settings::lon();
  const uint8_t range_index = core::settings::rangeIndex();
  if (!core::terrain::gridReady(lat, lon, range_index)) {
    return;
  }
  const core::terrain::Grid* grid = core::terrain::grid(range_index);
  if (grid == nullptr) {
    return;
  }

  initPixelToGridMap();
  // Grid row 0 is the north edge and column 0 the west edge, matching screen
  // y/x directly, so scanlines sample the grid without any axis flip.
  for (int y = 0; y < radar::kSize; ++y) {
    drawScanline(gfx, *grid, y);
  }
}

}  // namespace ui::terrain
