#pragma once

#include <LovyanGFX.hpp>

namespace ui::terrain {

/**
 * Paint the green-shaded elevation background across the whole frame.
 *
 * Draws nothing when the portal toggle is off or no grid is cached for the
 * current view, leaving the plain background fill visible. Call right after
 * the background fill and before the grid rings.
 */
void drawTerrainBackground(lgfx::LGFXBase& gfx);

}  // namespace ui::terrain
