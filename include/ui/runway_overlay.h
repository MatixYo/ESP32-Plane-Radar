#pragma once

#include <LovyanGFX.hpp>

namespace ui::runway {

/**
 * Draw the runways and ICAO labels of large airports in range. `band_top` is
 * the panel row at the top of `gfx`: 0, unless `gfx` holds one horizontal band
 * of the radar rather than all of it.
 */
void drawLargeAirportRunways(lgfx::LGFXBase& gfx, int band_top);

}  // namespace ui::runway
