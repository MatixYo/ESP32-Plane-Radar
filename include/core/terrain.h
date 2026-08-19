#pragma once

/**
 * Terrain elevation: sample an elevation grid around the radar centre from
 * terrain-RGB tiles and cache the one for the current view.
 *
 * The source is the AWS Open Data "Terrain Tiles" bucket (config::
 * kTerrainTileUrlFmt): one 256x256 PNG carries 65536 samples, so a view needs
 * only the 1-4 tiles its bounding box touches instead of the hundreds of
 * point queries an elevation API would charge for. Tiles are decoded as they
 * arrive — the raster is never held in RAM — through the PngDecodeFn seam, which
 * the platform wires to a decoder that borrows the frame sprite's pixels for
 * workspace, because on the device there is no spare RAM to decode into.
 *
 * The grid itself keeps the radar's own flat-earth convention (1 deg ~ 111 km
 * on BOTH axes, exactly like core::geo), so a grid point projects onto the
 * screen position the overlay samples it for, distortion included. Row 0 is
 * the north edge, column 0 the west edge.
 */

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "core/platform.h"

namespace core::terrain {

constexpr int kGridSize = config::kTerrainGridSize;
constexpr int kGridPoints = kGridSize * kGridSize;

/** Terrarium tiles are 256x256 px, the Web Mercator XYZ standard. */
constexpr int kTilePx = 256;

/** zoomForView() guarantees a view spans at most a 2x2 tile block. */
constexpr int kMaxTiles = 4;

/** terrain-tiles publishes zoom 0 through 15. */
constexpr int kMaxZoom = 15;

/** One fetched elevation grid, keyed by the view it was sampled for. */
struct Grid {
  bool valid = false;
  double center_lat = 0.0;
  double center_lon = 0.0;
  float half_span_km = 0.0f;         ///< centre to screen edge, km
  int16_t elev_m[kGridPoints] = {};  ///< row-major, row 0 = north edge
};

/** A Web Mercator XYZ tile. */
struct TileId {
  int z = 0;
  int x = 0;
  int y = 0;
};

/** Per-pixel sink: 8-bit RGB at tile-local coordinates. */
using PixelFn = void (*)(void* ctx, uint32_t x, uint32_t y, uint8_t r,
                         uint8_t g, uint8_t b);

/**
 * Streaming PNG decode seam. Returns false on a malformed or truncated image.
 *
 * Injected rather than called directly so core/ stays free of LovyanGFX: the
 * real decoder lives in platform/png_decode.cpp and is wired up in main.cpp,
 * while host tests supply their own. Without it ensureGrid() does nothing.
 */
using PngDecodeFn = bool (*)(platform::BodyReader& body, PixelFn on_pixel,
                             void* ctx);
void setPngDecoder(PngDecodeFn fn);

/** Poll hook invoked during HTTP I/O; wired to wifiLoop() in main.cpp. */
void setPollFn(platform::PollFn fn);

/** Invalidate the cached grid (e.g. after switching radar centre). */
void clear();

/** True while a download is in flight, i.e. mid-grid between tiles. */
bool downloadActive();

/** True if the cached grid matches this centre AND this range preset. */
bool gridReady(double center_lat, double center_lon, uint8_t range_index);

/**
 * Advance the download of the grid for this view unless it is already cached.
 *
 * Incremental: each call fetches and decodes at most ONE tile (blocking for
 * that request, polling meanwhile), spaced config::kTerrainTileIntervalMs
 * apart. Returns true only once every tile of the view has been folded into
 * the grid. A failed tile is retried in place without discarding the tiles
 * already decoded; one that keeps failing abandons the download until
 * config::kTerrainRetryIntervalMs has passed. A grid that finishes with samples
 * missing is discarded rather than published — gaps would paint as a false
 * shoreline, so no terrain is the better answer. Cheap to call every loop.
 *
 * Only one grid is cached, so changing range re-downloads; see the note on the
 * cache in the .cpp for why the ESP32-C3's heap makes that the right trade.
 */
bool ensureGrid(double center_lat, double center_lon, uint8_t range_index,
                float half_span_km);

/** The cached grid if it belongs to this range preset, else nullptr. */
const Grid* grid(uint8_t range_index);

// --- Pure helpers (exposed for unit testing) ---------------------------------

/**
 * Lat/lon of grid point (row, col) for a view. Row 0 = north edge (+dy),
 * col 0 = west edge (-dx); flat-earth km->deg via core::geo::kKmPerDeg.
 */
void pointLatLon(double center_lat, double center_lon, float half_span_km,
                 int row, int col, double* lat, double* lon);

/** Decode a terrarium pixel: height_m = R*256 + G + B/256 - 32768. */
int16_t terrariumElevation(uint8_t r, uint8_t g, uint8_t b);

/**
 * Web Mercator pixel coordinates at `zoom`, in the global 256*2^zoom space.
 * Latitude is clamped to the projection's +/-85.051129 deg limit.
 */
void latLonToTilePixel(double lat, double lon, int zoom, double* px,
                       double* py);

/**
 * Largest zoom whose tiles still cover the view with a 2x2 block, i.e. where
 * the grid's bounding box measures at most 256 px on both axes. Higher zoom
 * means finer terrain, so this picks the most detail we can fetch in <=4
 * requests.
 */
int zoomForView(double center_lat, double center_lon, float half_span_km);

/**
 * The unique tiles the view's bounding box touches at `zoom`, in raster order
 * (north-west first). Returns the count, at most kMaxTiles.
 */
size_t tilesForView(double center_lat, double center_lon, float half_span_km,
                    int zoom, TileId* out, size_t max_out);

void buildTileUrl(char* buf, size_t len, const TileId& tile);

/**
 * Band index for an elevation given ascending band floor altitudes:
 * -1 when elev_m < band_min_m[0] (water / sea level), else the highest i
 * with elev_m >= band_min_m[i].
 */
int bandForElevation(int16_t elev_m, const int16_t* band_min_m,
                     int band_count);

}  // namespace core::terrain
