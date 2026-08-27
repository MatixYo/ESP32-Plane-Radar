#pragma once

#include <cstdint>

namespace ui::radar {

constexpr int kSize = 240;
constexpr int kCenterX = kSize / 2;
constexpr int kCenterY = kSize / 2;

/** Outermost grid ring (inside edge labels). */
constexpr int kGridOuterRadius = 107;

/** N: offset from top edge (top_center, negative = up). */
constexpr int kCardinalNorthOffsetY = -1;
/** S: offset from bottom edge (bottom_center, positive = down). */
constexpr int kCardinalSouthOffsetY = 3;

/** Gap between scale label right edge and outer ring on the east spoke (px). */
constexpr int kScaleGapFromOuterRing = 6;

/** Target cap height (px) for N/S/E/W. */
constexpr int kCardinalLabelHeightPx = 14;
/** Scale label is this many px shorter than cardinals. */
constexpr int kScaleBelowCardinalPx = 3;

constexpr int kRingCount = 4;

/** Shared grid stroke: drawWideLine half-width (~2 px total); rings use the same px count. */
constexpr float kGridStrokeHalfWidth = 1.0f;

constexpr int kCenterDotRadius = 2;

/** Filled aircraft symbol (nose triangle). */
constexpr int kAircraftNoseLenPx = 8;
constexpr int kAircraftTailLenPx = 3;
constexpr int kAircraftTailHalfPx = 4;
/** drawWideLine half-width for the breadcrumb trail segments (~2 px total). */
constexpr float kAircraftTrackLineHalfWidth = 1.0f;

/** Climb/descent triangle on the altitude tag line: hidden when |vertical rate|
 *  is below this (ft/min) so level cruise stays uncluttered. */
constexpr float kVertRateThresholdFpm = 200.0f;
/** Filled triangle glyph: half-width, full height, gap before the altitude (px). */
constexpr int kVertRateGlyphHalfWpx = 4;
constexpr int kVertRateGlyphHpx = 8;
constexpr int kVertRateGlyphGapPx = 3;

constexpr float kRunwayLineWidthPx = 2.0f;
constexpr float kRunwayLineHalfWidth = kRunwayLineWidthPx * 0.5f;
constexpr int kRunwayLabelHeightPx = kCardinalLabelHeightPx;
constexpr int kRunwayLabelGapPx = 3;
/** Gap from triangle edge to tag block (px). */
constexpr int kAircraftLabelGapPx = 1;
/** Keep symbol centroid inside outer ring by at least this inset (px). */
constexpr int kAircraftInsideRingInsetPx =
    kAircraftNoseLenPx + kAircraftTailHalfPx + 1;

/** Beyond-ring traffic: bearing cues on screen rim (correct direction, fixed radius). */
constexpr int kBeyondRingDotRadiusPx = 4;
constexpr int kBeyondRingScreenMarginPx = 2;
/** Target cap height (px) for aircraft tags (bold, slightly above scale label). */
constexpr int kAircraftTagLabelHeightPx = 13;
/** Overlapping tags take turns: each cluster member is shown for this long. */
constexpr uint32_t kAircraftTagCycleMs = 2000;
/** AABB slack when testing whether two tag blocks overlap (px). */
constexpr int kAircraftTagOverlapPadPx = 2;

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
/** Breadcrumb trail — yellow, faded per segment toward the oldest fix. */
constexpr uint8_t kTrackR = 255;
constexpr uint8_t kTrackG = 225;
constexpr uint8_t kTrackB = 0;
constexpr uint8_t kTagTypeR = 255;
constexpr uint8_t kTagTypeG = 200;
constexpr uint8_t kTagTypeB = 0;
constexpr uint8_t kTagAltR = 90;
constexpr uint8_t kTagAltG = 200;
constexpr uint8_t kTagAltB = 255;
/** Route line (origin > destination) — muted green, below the altitude. */
constexpr uint8_t kTagRouteR = 150;
constexpr uint8_t kTagRouteG = 230;
constexpr uint8_t kTagRouteB = 150;
/** Vertical-rate triangle: green for a climb, amber for a descent. */
constexpr uint8_t kVertClimbR = 60;
constexpr uint8_t kVertClimbG = 220;
constexpr uint8_t kVertClimbB = 90;
constexpr uint8_t kVertDescentR = 255;
constexpr uint8_t kVertDescentG = 160;
constexpr uint8_t kVertDescentB = 0;
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
extern uint16_t kColorVertClimb;
extern uint16_t kColorVertDescent;
extern uint16_t kColorTagType;
extern uint16_t kColorTagAltitude;
extern uint16_t kColorTagRoute;
extern uint16_t kColorRunway;
extern uint16_t kColorRunwayLabel;

}  // namespace ui::radar
