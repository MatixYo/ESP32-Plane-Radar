#pragma once

#include <cstdint>

#include "config.h"

namespace ui::radar {

inline constexpr int scalePx(int v) {
  return v * config::kDisplayWidth / config::kDisplayBaseSize;
}

inline constexpr float scalePxF(float v) {
  return v * static_cast<float>(config::kDisplayWidth) /
         static_cast<float>(config::kDisplayBaseSize);
}

constexpr int kSize = config::kDisplayWidth;
constexpr int kCenterX = kSize / 2;
constexpr int kCenterY = kSize / 2;

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
constexpr float kGridStrokeHalfWidth = scalePxF(1.0f);

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
constexpr float kAircraftTrackLineHalfWidth = scalePxF(1.0f);

constexpr float kRunwayLineWidthPx = scalePxF(2.0f);
constexpr float kRunwayLineHalfWidth = kRunwayLineWidthPx * 0.5f;
constexpr int kRunwayLabelHeightPx = kCardinalLabelHeightPx;
constexpr int kRunwayLabelGapPx = scalePx(3);
/** Gap from triangle edge to tag block (px). */
constexpr int kAircraftLabelGapPx = scalePx(1);
/** Keep symbol centroid inside outer ring by at least this inset (px). */
constexpr int kAircraftInsideRingInsetPx =
    kAircraftNoseLenPx + kAircraftTailHalfPx + scalePx(1);

/** Beyond-ring traffic: bearing cues on screen rim (correct direction, fixed radius). */
constexpr int kBeyondRingDotRadiusPx = scalePx(4);
constexpr int kBeyondRingScreenMarginPx = scalePx(2);
/** Target cap height (px) for aircraft tags (bold, slightly above scale label). */
constexpr int kAircraftTagLabelHeightPx = scalePx(13);

/** RGB565 palette targets (applied in initPalette). */
// Night — classic sonar (default).
constexpr uint8_t kBgR = 4;
constexpr uint8_t kBgG = 10;
constexpr uint8_t kBgB = 28;
constexpr uint8_t kGridR = 16;
constexpr uint8_t kGridG = 100;
constexpr uint8_t kGridB = 32;
constexpr uint8_t kLabelR = 255;
constexpr uint8_t kLabelG = 255;
constexpr uint8_t kLabelB = 255;
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
constexpr uint8_t kRunwayLabelR = 110;
constexpr uint8_t kRunwayLabelG = 210;
constexpr uint8_t kRunwayLabelB = 230;

// Day — light background, dark grid and labels.
constexpr uint8_t kDayBgR = 245;
constexpr uint8_t kDayBgG = 248;
constexpr uint8_t kDayBgB = 252;
constexpr uint8_t kDayGridR = 24;
constexpr uint8_t kDayGridG = 130;
constexpr uint8_t kDayGridB = 56;
constexpr uint8_t kDayLabelR = 24;
constexpr uint8_t kDayLabelG = 28;
constexpr uint8_t kDayLabelB = 36;
constexpr uint8_t kDayAircraftR = 210;
constexpr uint8_t kDayAircraftG = 0;
constexpr uint8_t kDayAircraftB = 0;
constexpr uint8_t kDayTrackR = 130;
constexpr uint8_t kDayTrackG = 0;
constexpr uint8_t kDayTrackB = 170;
constexpr uint8_t kDayTagTypeR = 170;
constexpr uint8_t kDayTagTypeG = 90;
constexpr uint8_t kDayTagTypeB = 0;
constexpr uint8_t kDayTagAltR = 0;
constexpr uint8_t kDayTagAltG = 90;
constexpr uint8_t kDayTagAltB = 150;
constexpr uint8_t kDayRunwayR = 36;
constexpr uint8_t kDayRunwayG = 105;
constexpr uint8_t kDayRunwayB = 118;
constexpr uint8_t kDayRunwayLabelR = 18;
constexpr uint8_t kDayRunwayLabelG = 95;
constexpr uint8_t kDayRunwayLabelB = 108;

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
