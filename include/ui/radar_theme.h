#pragma once

#include <cstdint>

namespace ui::radar {

/**
 * Radar layout.
 *
 * Lengths were chosen against a 240x240 panel and are scaled at runtime by
 * initMetrics() to the panel actually fitted, so they are variables rather than
 * constants — the same shape the colours below have always had. Values that are
 * not lengths (counts, times, km, ratios) stay constexpr.
 */

/** The panel this layout was designed on; every scaled value is a ratio of it. */
constexpr int kReferenceSize = 240;

/**
 * Recompute the layout for a panel of this size. Call once, after `tft.init()`
 * and before anything draws. `panel_width`/`panel_height` come from
 * `tft.width()` / `tft.height()` rather than from `config.h`, so a panel that
 * reports a size different from the configured one still lays out correctly.
 */
void initMetrics(int panel_width, int panel_height);

/** Scale actually applied, = min(width, height) / kReferenceSize. 1.0 on 240px. */
extern float kScale;

/** Square extent of the radar, = min(panel width, panel height). */
extern int kSize;
extern int kCenterX;
extern int kCenterY;

/** Outermost grid ring (inside edge labels). */
extern int kGridOuterRadius;

/** N: offset from top edge (top_center, negative = up). */
extern int kCardinalNorthOffsetY;
/** S: offset from bottom edge (bottom_center, positive = down). */
extern int kCardinalSouthOffsetY;

/** Gap between scale label right edge and outer ring on the east spoke (px). */
extern int kScaleGapFromOuterRing;

/** Target cap height (px) for N/S/E/W. */
extern int kCardinalLabelHeightPx;
/** Scale label is this many px shorter than cardinals. */
extern int kScaleBelowCardinalPx;

/** A count, not a length — the same four rings at any panel size. */
constexpr int kRingCount = 4;

/** Shared grid stroke: drawWideLine half-width; rings use the same px count. */
extern float kGridStrokeHalfWidth;

extern int kCenterDotRadius;

/** Filled aircraft symbol (nose triangle). */
extern int kAircraftNoseLenPx;
extern int kAircraftTailLenPx;
extern int kAircraftTailHalfPx;

/** Track vector: ground distance covered in this many seconds at current gs. */
constexpr float kAircraftTrackHorizonSec = 60.0f;
/** Track line length uses this outer_km, not the active range preset. */
constexpr float kAircraftTrackRefOuterKm = 13.3f;
/** Shorter than full 60 s horizon at ref scale; x1.5 length boost applied. */
constexpr float kAircraftTrackLengthScale = 1.5f / 5.0f;

/** Minimum visible vector when gs > 0 (px). */
extern int kAircraftSpeedLineMinPx;
/** drawWideLine half-width for speed vectors. */
extern float kAircraftTrackLineHalfWidth;

extern float kRunwayLineWidthPx;
extern float kRunwayLineHalfWidth;
extern int kRunwayLabelHeightPx;
extern int kRunwayLabelGapPx;

/** Gap from triangle edge to tag block (px). */
extern int kAircraftLabelGapPx;
/** Keep symbol centroid inside outer ring by at least this inset (px). */
extern int kAircraftInsideRingInsetPx;

/** Beyond-ring traffic: bearing cues on screen rim (correct direction, fixed radius). */
extern int kBeyondRingDotRadiusPx;
extern int kBeyondRingScreenMarginPx;
/** Target cap height (px) for aircraft tags (bold, slightly above scale label). */
extern int kAircraftTagLabelHeightPx;

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
