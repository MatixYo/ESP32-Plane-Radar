#include "ui/radar_theme.h"

#include <cmath>

namespace ui::radar {

// Defaults are the reference design, so anything that draws before
// initMetrics() runs still lays out correctly on a 240x240 panel.
float kScale = 1.0f;

int kSize = kReferenceSize;
int kCenterX = kReferenceSize / 2;
int kCenterY = kReferenceSize / 2;

int kGridOuterRadius = 107;

int kCardinalNorthOffsetY = -1;
int kCardinalSouthOffsetY = 3;

int kScaleGapFromOuterRing = 6;

int kCardinalLabelHeightPx = 14;
int kScaleBelowCardinalPx = 3;

float kGridStrokeHalfWidth = 1.0f;

int kCenterDotRadius = 2;

int kAircraftNoseLenPx = 8;
int kAircraftTailLenPx = 3;
int kAircraftTailHalfPx = 4;

int kAircraftSpeedLineMinPx = 2;
float kAircraftTrackLineHalfWidth = 1.0f;

float kRunwayLineWidthPx = 2.0f;
float kRunwayLineHalfWidth = 1.0f;
int kRunwayLabelHeightPx = 14;
int kRunwayLabelGapPx = 3;

int kAircraftLabelGapPx = 1;
int kAircraftInsideRingInsetPx = 8 + 4 + 1;

int kBeyondRingDotRadiusPx = 4;
int kBeyondRingScreenMarginPx = 2;
int kAircraftTagLabelHeightPx = 13;

namespace {

/**
 * Scale a reference-design length to the fitted panel. lroundf rather than a
 * cast, which would truncate half a pixel off every value; a length must not
 * round away to nothing, or a stroke or dot disappears.
 */
static inline int scaled(int reference_px) {
  const int value =
      static_cast<int>(lroundf(static_cast<float>(reference_px) * kScale));
  if (reference_px > 0 && value < 1) {
    return 1;
  }
  if (reference_px < 0 && value > -1) {
    return -1;
  }
  return value;
}

static inline float scaledF(float reference_px) {
  return reference_px * kScale;
}

}  // namespace

void initMetrics(int panel_width, int panel_height) {
  // The radar is round, so it lives in the largest square the panel can hold.
  kSize = (panel_width < panel_height) ? panel_width : panel_height;
  kCenterX = panel_width / 2;
  kCenterY = panel_height / 2;
  kScale = static_cast<float>(kSize) / static_cast<float>(kReferenceSize);

  kGridOuterRadius = scaled(107);

  kCardinalNorthOffsetY = scaled(-1);
  kCardinalSouthOffsetY = scaled(3);

  kScaleGapFromOuterRing = scaled(6);

  kCardinalLabelHeightPx = scaled(14);
  kScaleBelowCardinalPx = scaled(3);

  kGridStrokeHalfWidth = scaledF(1.0f);

  kCenterDotRadius = scaled(2);

  kAircraftNoseLenPx = scaled(8);
  kAircraftTailLenPx = scaled(3);
  kAircraftTailHalfPx = scaled(4);

  kAircraftSpeedLineMinPx = scaled(2);
  kAircraftTrackLineHalfWidth = scaledF(1.0f);

  kRunwayLineWidthPx = scaledF(2.0f);
  kRunwayLabelGapPx = scaled(3);

  kAircraftLabelGapPx = scaled(1);

  kBeyondRingDotRadiusPx = scaled(4);
  kBeyondRingScreenMarginPx = scaled(2);
  kAircraftTagLabelHeightPx = scaled(13);

  // Derived from the values above, keeping the original formulas rather than
  // scaling their results — the +1 below is a one-pixel safety margin that the
  // reference design applied after the symbol arithmetic, not before it.
  kRunwayLineHalfWidth = kRunwayLineWidthPx * 0.5f;
  kRunwayLabelHeightPx = kCardinalLabelHeightPx;
  kAircraftInsideRingInsetPx = kAircraftNoseLenPx + kAircraftTailHalfPx + 1;
}

}  // namespace ui::radar
