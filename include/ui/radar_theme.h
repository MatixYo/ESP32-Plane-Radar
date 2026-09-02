#pragma once

#include <cstdint>

#include "config.h"

namespace ui::radar {

constexpr int kSize = config::kRadarViewportSize;
constexpr int kBaseSize = 240;
constexpr int kOffsetX = config::kRadarViewportX;
constexpr int kOffsetY = config::kRadarViewportY;
constexpr int kRight = kOffsetX + kSize - 1;
constexpr int kBottom = kOffsetY + kSize - 1;
constexpr int kCenterX = kOffsetX + kSize / 2;
constexpr int kCenterY = kOffsetY + kSize / 2;

// Keep the original 240×240 radar geometry unchanged on existing boards.
// Larger rectangular boards scale these pixel constants from the 240 px baseline.
constexpr int scalePx(int px) { return (px * kSize + kBaseSize / 2) / kBaseSize; }
constexpr float scalePx(float px) {
  return px * static_cast<float>(kSize) / static_cast<float>(kBaseSize);
}

/** Outermost grid ring (inside edge labels). */
constexpr int kGridOuterRadius = scalePx(107);

/** N: offset from top edge (top_center, negative = up). */
constexpr int kCardinalNorthOffsetY = scalePx(-1);
/** S: offset from bottom edge (bottom_center, positive = down). */
constexpr int kCardinalSouthOffsetY = scalePx(3);

/** Gap between scale label right edge and outer ring on the east spoke (px). */
constexpr int kScaleGapFromOuterRing = scalePx(6);

/** Target cap height (px) for N/S/E/W. */
constexpr int kCardinalLabelHeightPx = scalePx(14);
/** Scale label is this many px shorter than cardinals. */
constexpr int kScaleBelowCardinalPx = scalePx(3);

constexpr int kRingCount = 4;

/** Shared grid stroke: drawWideLine half-width (~2 px total); rings use the same px count. */
constexpr float kGridStrokeHalfWidth = scalePx(1.0f);

constexpr int kCenterDotRadius = scalePx(2);

/** Filled aircraft symbol (nose triangle). */
constexpr int kAircraftNoseLenPx = scalePx(8);
constexpr int kAircraftTailLenPx = scalePx(3);
constexpr int kAircraftTailHalfPx = scalePx(4);
/** Track vector: ground distance covered in this many seconds at current gs. */
constexpr float kAircraftTrackHorizonSec = 60.0f;
/** Minimum visible vector when gs > 0 (px). */
constexpr int kAircraftSpeedLineMinPx = scalePx(2);
/** Track line length uses this outer_km, not the active range preset. */
constexpr float kAircraftTrackRefOuterKm = 13.3f;
/** Shorter than full 60 s horizon at ref scale; ×1.5 length boost applied. */
constexpr float kAircraftTrackLengthScale = 1.5f / 5.0f;
/** drawWideLine half-width for speed vectors (~2 px total). */
constexpr float kAircraftTrackLineHalfWidth = scalePx(1.0f);

constexpr float kRunwayLineWidthPx = scalePx(2.0f);
constexpr float kRunwayLineHalfWidth = kRunwayLineWidthPx * 0.5f;
constexpr int kRunwayLabelHeightPx = kCardinalLabelHeightPx;
constexpr int kRunwayLabelGapPx = scalePx(3);
/** Gap from triangle edge to tag block (px). */
constexpr int kAircraftLabelGapPx = scalePx(1);
/** Keep symbol centroid inside outer ring by at least this inset (px). */
constexpr int kAircraftInsideRingInsetPx =
    kAircraftNoseLenPx + kAircraftTailHalfPx + 1;

/** Beyond-ring traffic: bearing cues on screen rim (correct direction, fixed radius). */
constexpr int kBeyondRingDotRadiusPx = scalePx(4);
constexpr int kBeyondRingScreenMarginPx = scalePx(2);
/** Target cap height (px) for aircraft tags (bold, slightly above scale label). */
constexpr int kAircraftTagLabelHeightPx = scalePx(13);

/** RGB565 palette targets (applied in initPalette). */
constexpr uint8_t kBgR = 4;
constexpr uint8_t kBgG = 10;
constexpr uint8_t kBgB = 28;
constexpr uint8_t kGridR = 16;
constexpr uint8_t kGridG = 100;
constexpr uint8_t kGridB = 32;
constexpr uint8_t kAircraftR = 255;
constexpr uint8_t kAircraftG = 0;
constexpr uint8_t kAircraftB = 0;
constexpr uint8_t kTrackR = 255;
constexpr uint8_t kTrackG = 0;
constexpr uint8_t kTrackB = 255;
constexpr uint8_t kTagTypeR = 255;
constexpr uint8_t kTagTypeG = 200;
constexpr uint8_t kTagTypeB = 0;
constexpr uint8_t kTagAltR = 90;
constexpr uint8_t kTagAltG = 200;
constexpr uint8_t kTagAltB = 255;
constexpr uint8_t kRunwayR = 56;
constexpr uint8_t kRunwayG = 150;
constexpr uint8_t kRunwayB = 170;
/** Lighter teal for ICAO labels (vs runway lines). */
constexpr uint8_t kRunwayLabelR = 110;
constexpr uint8_t kRunwayLabelG = 210;
constexpr uint8_t kRunwayLabelB = 230;

extern uint16_t kColorBackground;
extern uint16_t kColorGrid;
extern uint16_t kColorLabel;
extern uint16_t kColorCenter;
extern uint16_t kColorAircraft;
extern uint16_t kColorTrackVector;
extern uint16_t kColorTagType;
extern uint16_t kColorTagAltitude;
extern uint16_t kColorRunway;
extern uint16_t kColorRunwayLabel;

}  // namespace ui::radar
