#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <time.h>

#ifdef BOARD_NM_TV_154
#include <WiFi.h>
#endif

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/time_settings.h"
#include "ui/radar_animation_policy.h"
#include "ui/radar_range.h"
#include "ui/radar_render_policy.h"
#include "ui/radar_tag_layout_policy.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"
#ifdef BOARD_NM_TV_154
#include "ui/square_status.h"
#include "ui/nm_tv_154_policy.h"
#endif

namespace radar_fonts = lgfx::v1::fonts;

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;

}  // namespace radar

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
bool s_corner_value_use_vlw = false;
float s_corner_value_vlw_size = 0.9f;
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &radar_fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &radar_fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_corner_value_gfx = &radar_fonts::FreeSansBold18pt7b;
const lgfx::GFXfont* s_tag_gfx = &radar_fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;
bool s_frame_attempted = false;
uint16_t s_sweep_bright = 0;
uint16_t s_sweep_mid = 0;
uint16_t s_sweep_dim = 0;

#ifdef BOARD_NM_TV_154
constexpr unsigned long kFrameBudgetMs = 50UL;
constexpr uint8_t kFrameTimingSampleCount = 20;
unsigned long s_frame_total_ms = 0;
unsigned long s_frame_max_ms = 0;
uint8_t s_frame_timing_samples = 0;
bool s_has_data_update = false;
unsigned long s_last_data_update_ms = 0;
uint16_t s_corner_green = 0;
uint16_t s_corner_amber = 0;
uint16_t s_corner_cyan = 0;
uint16_t s_corner_red = 0;
uint16_t s_corner_muted = 0;
#endif

#ifdef BOARD_NM_TV_154
bool s_has_data_update = false;
unsigned long s_last_data_update_ms = 0;
uint16_t s_corner_green = 0;
uint16_t s_corner_amber = 0;
uint16_t s_corner_cyan = 0;
uint16_t s_corner_red = 0;
uint16_t s_corner_muted = 0;
#endif

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

void recordFrameDuration(unsigned long elapsed_ms) {
#ifdef BOARD_NM_TV_154
  s_frame_total_ms += elapsed_ms;
  if (elapsed_ms > s_frame_max_ms) {
    s_frame_max_ms = elapsed_ms;
  }
  ++s_frame_timing_samples;
  if (s_frame_timing_samples < kFrameTimingSampleCount) {
    return;
  }

  const unsigned long average_ms = s_frame_total_ms / s_frame_timing_samples;
  Serial.printf("radar: 8-bit frame avg=%lums max=%lums 20fps=%s\n", average_ms,
                s_frame_max_ms,
                s_frame_max_ms <= kFrameBudgetMs ? "PASS" : "SLOW");
  s_frame_total_ms = 0;
  s_frame_max_ms = 0;
  s_frame_timing_samples = 0;
#else
  (void)elapsed_ms;
#endif
}

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;
  constexpr int kCornerValueTargetHeightPx = 24;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
    s_corner_value_use_vlw = true;
    s_corner_value_vlw_size = findVlwSizeForHeight(kCornerValueTargetHeightPx);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {
        &radar_fonts::FreeSansBold12pt7b,
        &radar_fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {
        &radar_fonts::FreeSansBold9pt7b,
        &radar_fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
    s_corner_value_gfx = &radar_fonts::FreeSansBold18pt7b;
    s_corner_value_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  char label[12];
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    for (bool miles : {false, true}) {
      radar::formatRing3Label(label, sizeof(label), radar::kRangePresets[i].ring3_km,
                              miles);
      const int w = tft.textWidth(label);
      if (w > s_scale_label_max_w) {
        s_scale_label_max_w = w;
      }
    }
  }

  s_label_metrics_ready = true;
}

void initTagLabelMetrics() {
  if (s_tag_label_metrics_ready) {
    return;
  }

#ifdef BOARD_NM_TV_154
  s_tag_gfx = &radar_fonts::FreeSansBold9pt7b;
  s_tag_use_vlw = false;
#else
  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {
        &radar_fonts::FreeSansBold12pt7b,
        &radar_fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }
#endif

  s_tag_label_metrics_ready = true;
}

uint16_t logicalColor565(uint8_t red, uint8_t green, uint8_t blue) {
  if (config::kDisplayRgbOrder) {
    return tft.color565(blue, green, red);
  }
  return tft.color565(red, green, blue);
}

void initPalette() {
  radar::kColorBackground = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
  radar::kColorGrid = tft.color565(radar::kGridR, radar::kGridG, radar::kGridB);
  radar::kColorLabel = tft.color565(255, 255, 255);
  radar::kColorCenter = tft.color565(255, 255, 255);
  // GC9A01 BGR panel: swap R/B in color565 so logical red renders red on screen.
  if (config::kDisplayRgbOrder) {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR);
  } else {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB);
  }
  radar::kColorTrackVector =
      tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
  radar::kColorTagType =
      tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
  radar::kColorTagAltitude =
      tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  radar::kColorRunway =
      tft.color565(radar::kRunwayR, radar::kRunwayG, radar::kRunwayB);
  radar::kColorRunwayLabel = tft.color565(radar::kRunwayLabelR, radar::kRunwayLabelG,
                                          radar::kRunwayLabelB);
  s_sweep_bright = logicalColor565(50, 220, 105);
  s_sweep_mid = logicalColor565(25, 120, 60);
  s_sweep_dim = logicalColor565(12, 65, 34);
#ifdef BOARD_NM_TV_154
  s_corner_green = logicalColor565(54, 220, 110);
  s_corner_amber = logicalColor565(255, 190, 70);
  s_corner_cyan = logicalColor565(65, 205, 235);
  s_corner_red = logicalColor565(255, 75, 75);
  s_corner_muted = logicalColor565(95, 125, 145);
#endif
}

constexpr float kKmPerDeg = 111.0f;

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  *dx_km =
      static_cast<float>(lon - services::location::lon()) * kKmPerDeg;
  *dy_km =
      static_cast<float>(lat - services::location::lat()) * kKmPerDeg;
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

float innerRingMaxKm() {
  const float outer_km = radar::rangeCurrent().outer_km;
  return outer_km * (static_cast<float>(radar::kGridOuterRadius -
                                       radar::kAircraftInsideRingInsetPx) /
                     static_cast<float>(radar::kGridOuterRadius));
}

/** Flat lat/lon as x/y: 1° ≈ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = radar::rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);

  *out_x = radar::kCenterX + static_cast<int>(lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(dy_km * px_per_km));
}

bool isInsideOuterRingKm(float dist_km) { return dist_km <= innerRingMaxKm(); }

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

bool isInsideOuterRing(int x, int y) {
  const int max_r = radar::kGridOuterRadius - radar::kAircraftInsideRingInsetPx;
  return distSqFromCenter(x, y) <= max_r * max_r;
}

/** Rim dot from true bearing; always on screen edge (even if target is 50+ km away). */
bool beyondRingEdgeDotFromLatLon(float lat, float lon, int* out_x, int* out_y) {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  if (dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(dist_km)) {
    return false;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
  const float angle_rad = atan2f(dx_km, dy_km);

  *out_x = cx + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  *out_y = cy - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void drawBeyondRingDot(int x, int y) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx,
                           radar::kColorAircraft);
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r = radar::kGridOuterRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * radar::kAircraftNoseLenPx));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * radar::kAircraftNoseLenPx));
}

void drawHeadingTriangle(int cx, int cy, float heading_deg, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  const int base_x =
      cx - static_cast<int>(lroundf(sin_h * static_cast<float>(radar::kAircraftTailLenPx)));
  const int base_y =
      cy + static_cast<int>(lroundf(cos_h * static_cast<float>(radar::kAircraftTailLenPx)));

  const int wing_x = static_cast<int>(lroundf(cos_h * radar::kAircraftTailHalfPx));
  const int wing_y = static_cast<int>(lroundf(sin_h * radar::kAircraftTailHalfPx));

  s_draw->fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y,
                       base_x - wing_x, base_y - wing_y, color);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawWideLine(tip_x, tip_y, ex, ey, radar::kAircraftTrackLineHalfWidth,
                       color);
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
#ifdef BOARD_NM_TV_154
    constexpr float kNmTv154TagScale = 0.90f;
    s_draw->setTextSize(kNmTv154TagScale);
#endif
  }
}

enum class TagDetail {
  kFull,
  kCompact,
};

bool tagShowsType(TagDetail detail) { return detail == TagDetail::kFull; }

int tagLineCount(const services::adsb::Aircraft& plane, TagDetail detail) {
  int count = plane.callsign[0] != '\0' ? 1 : 0;
  if (tagShowsType(detail) && plane.type[0] != '\0') {
    ++count;
  }
  if (plane.alt[0] != '\0') {
    ++count;
  }
  return count;
}

int measureTagBlockWidth(const services::adsb::Aircraft& plane, TagDetail detail) {
  applyTagStyle();
  int max_w = 0;
  if (plane.callsign[0] != '\0') {
    const int w = s_draw->textWidth(plane.callsign);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (tagShowsType(detail) && plane.type[0] != '\0') {
    const int w = s_draw->textWidth(plane.type);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.alt[0] != '\0') {
    const int w = s_draw->textWidth(plane.alt);
    if (w > max_w) {
      max_w = w;
    }
  }
  return max_w;
}

int tagBlockHeight(const services::adsb::Aircraft& plane, TagDetail detail) {
  applyTagStyle();
  return s_draw->fontHeight() * tagLineCount(plane, detail);
}

void drawAircraftTag(int x, int y, const services::adsb::Aircraft& plane,
                     TagDetail detail) {
  initTagLabelMetrics();
  applyTagStyle();

  const int line_h = s_draw->fontHeight();
  int ly = y;
  s_draw->setTextDatum(textdatum_t::top_left);

  if (plane.callsign[0] != '\0') {
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    s_draw->drawString(plane.callsign, x, ly);
    ly += line_h;
  }

  if (tagShowsType(detail) && plane.type[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
    s_draw->drawString(plane.type, x, ly);
    ly += line_h;
  }

  if (plane.alt[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagAltitude, radar::kColorBackground);
    s_draw->drawString(plane.alt, x, ly);
  }
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct BeyondDotDrawItem {
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct TagPlacement {
  radar::TagRect rect = {0, 0, 0, 0};
  TagDetail detail = TagDetail::kFull;
  bool valid = false;
};

bool tagConflictsWithHud(radar::TagRect rect) {
#ifdef BOARD_NM_TV_154
  constexpr radar::TagRect kHudRegions[] = {
      {0, 0, 70, 44},
      {170, 0, radar::kSize, 44},
      {0, 196, 76, radar::kSize},
      {164, 196, radar::kSize, radar::kSize},
  };
  for (const radar::TagRect& region : kHudRegions) {
    if (radar::tagRectsOverlap(rect, region)) {
      return true;
    }
  }
#else
  (void)rect;
#endif
  return false;
}

bool tagConflictsWithPlaced(radar::TagRect rect, const radar::TagRect* placed,
                             size_t placed_count) {
  for (size_t i = 0; i < placed_count; ++i) {
    if (radar::tagRectsOverlap(rect, placed[i])) {
      return true;
    }
  }
  return false;
}

bool tagOverlapsAircraftSymbol(radar::TagRect rect, const AircraftDrawItem* items,
                               size_t item_count) {
  constexpr int kSymbolHalf =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  for (size_t i = 0; i < item_count; ++i) {
    const radar::TagRect symbol = {
        items[i].x - kSymbolHalf,
        items[i].y - kSymbolHalf,
        items[i].x + kSymbolHalf + 1,
        items[i].y + kSymbolHalf + 1,
    };
    if (radar::tagRectsOverlap(rect, symbol)) {
      return true;
    }
  }
  return false;
}

TagPlacement findTagPlacement(const services::adsb::Aircraft& plane, int x, int y,
                              TagDetail detail, const AircraftDrawItem* items,
                              size_t item_count, const radar::TagRect* placed,
                              size_t placed_count) {
  const int width = measureTagBlockWidth(plane, detail);
  const int height = tagBlockHeight(plane, detail);
  if (width <= 0 || height <= 0) {
    return {};
  }

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  const int gap = symbol_half + radar::kAircraftLabelGapPx;
  const int left = x - gap - width;
  const int right = x + gap;
  const int middle_x = x - width / 2;
  const int top = y - gap - height;
  const int bottom = y + gap;
  const int middle_y = y - height / 2;
  const int outward_x = x < radar::kCenterX ? left : right;
  const int inward_x = x < radar::kCenterX ? right : left;
  const int outward_y = y < radar::kCenterY ? top : bottom;
  const int inward_y = y < radar::kCenterY ? bottom : top;
  const radar::TagRect candidates[] = {
      {outward_x, middle_y, outward_x + width, middle_y + height},
      {middle_x, outward_y, middle_x + width, outward_y + height},
      {outward_x, outward_y, outward_x + width, outward_y + height},
      {outward_x, inward_y, outward_x + width, inward_y + height},
      {inward_x, outward_y, inward_x + width, outward_y + height},
      {inward_x, middle_y, inward_x + width, middle_y + height},
      {middle_x, inward_y, middle_x + width, inward_y + height},
      {inward_x, inward_y, inward_x + width, inward_y + height},
  };

  for (const radar::TagRect& candidate : candidates) {
    if (!radar::tagRectInside(candidate, radar::kSize, radar::kSize) ||
        tagConflictsWithHud(candidate) ||
        tagConflictsWithPlaced(candidate, placed, placed_count) ||
        tagOverlapsAircraftSymbol(candidate, items, item_count)) {
      continue;
    }
    return {candidate, detail, true};
  }
  return {};
}

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void drawAircraft(unsigned long now_ms) {
  initLabelMetrics();

  size_t n = 0;
  const services::adsb::Aircraft* planes =
      services::adsb::aircraftList(now_ms, &n);

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[services::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;

  for (size_t i = 0; i < n; ++i) {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx_km, &dy_km, &dist_km);

    if (isInsideOuterRingKm(dist_km)) {
      int x = 0;
      int y = 0;
      latLonToScreen(planes[i].lat, planes[i].lon, &x, &y);
      items[draw_count].index = i;
      items[draw_count].x = x;
      items[draw_count].y = y;
      items[draw_count].dist_sq = distSqFromCenter(x, y);
      ++draw_count;
      continue;
    }

    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(planes[i].lat, planes[i].lon, &dot_x,
                                     &dot_y)) {
      continue;
    }
    dots[dot_count].x = dot_x;
    dots[dot_count].y = dot_y;
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    ++dot_count;
  }

  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    drawBeyondRingDot(dots[d].x, dots[d].y);
  }

  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    drawSpeedVector(x, y, planes[i].nose_deg, planes[i].track_deg,
                    planes[i].gs_knots, radar::kColorTrackVector);
    drawHeadingTriangle(x, y, planes[i].nose_deg, radar::kColorAircraft);
  }
  radar::TagRect placed_tags[services::adsb::kMaxAircraft];
  size_t placed_tag_count = 0;
  // Items are far-first; reverse the order so the closest traffic owns space first.
  for (size_t d = draw_count; d > 0; --d) {
    const AircraftDrawItem& item = items[d - 1];
    const services::adsb::Aircraft& plane = planes[item.index];
    TagPlacement placement = findTagPlacement(
        plane, item.x, item.y, TagDetail::kFull, items, draw_count, placed_tags,
        placed_tag_count);
    if (!placement.valid) {
      placement = findTagPlacement(plane, item.x, item.y, TagDetail::kCompact,
                                   items, draw_count, placed_tags, placed_tag_count);
    }
    if (!placement.valid) {
      continue;
    }
    drawAircraftTag(placement.rect.left, placement.rect.top, plane,
                    placement.detail);
    placed_tags[placed_tag_count++] = placement.rect;
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_scale_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
  }
}

void applyCornerValueStyle() {
  if (s_corner_value_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_corner_value_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_corner_value_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  s_draw->drawWideLine(cx, cy - radius, cx, cy + radius,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx - radius, cy, cx + radius, cy,
                       radar::kGridStrokeHalfWidth, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

#ifdef BOARD_NM_TV_154
void drawCornerTime(const nm_tv_154::CornerTelemetryLayout& layout,
                    int value_height, int right, bool clear_value_area) {
  char value[12];
  tm local_time = {};
  uint16_t time_color = s_corner_muted;
  if (getLocalTime(&local_time, 0)) {
    strftime(value, sizeof(value),
             services::time_settings::uses24HourClock() ? "%H:%M" : "%I:%M %p",
             &local_time);
    time_color = s_corner_cyan;
  } else {
    snprintf(value, sizeof(value), "--");
  }

  applyCornerValueStyle();
  if (clear_value_area) {
    const int max_width = s_draw->textWidth("88:88 PM");
    s_draw->fillRect(right - max_width, layout.bottom_value_y - value_height,
                     max_width + 1, value_height + 1, radar::kColorBackground);
  }
  s_draw->setTextDatum(textdatum_t::bottom_right);
  s_draw->setTextColor(time_color, radar::kColorBackground);
  s_draw->drawString(value, right, layout.bottom_value_y);
}
#endif

void drawCornerTelemetry() {
#ifdef BOARD_NM_TV_154
  constexpr int kEdge = 5;
  constexpr int kValueGap = 2;
  const int kRight = radar::kSize - kEdge;

  const bool wifi_connected = WiFi.status() == WL_CONNECTED;

  s_draw->setFont(&radar_fonts::Font0);
  s_draw->setTextSize(1);
  const int label_height = s_draw->fontHeight();
  applyCornerValueStyle();
  const int value_height = s_draw->fontHeight();
  const nm_tv_154::CornerTelemetryLayout layout =
      nm_tv_154::cornerTelemetryLayout(radar::kSize, kEdge, label_height,
                                       value_height, kValueGap);

  s_draw->setFont(&radar_fonts::Font0);
  s_draw->setTextSize(1);
  s_draw->setTextColor(s_corner_muted);
  s_draw->setTextDatum(textdatum_t::top_left);
  s_draw->drawString("WIFI", kEdge, layout.top_label_y);

  const uint8_t bars =
      square::wifiBars(wifi_connected, wifi_connected ? WiFi.RSSI() : -100);
  if (wifi_connected) {
    constexpr int kBarWidth = 3;
    constexpr int kBarGap = 2;
    constexpr int kBarHeights[] = {3, 5, 8, 11};
    const int bar_baseline = layout.top_value_y + value_height;
    for (uint8_t i = 0; i < 4; ++i) {
      const int x = kEdge + i * (kBarWidth + kBarGap);
      const int height = kBarHeights[i];
      const uint16_t color = i < bars ? s_corner_green : s_corner_muted;
      s_draw->fillRect(x, bar_baseline - height + 1, kBarWidth, height, color);
    }
  } else {
    s_draw->drawLine(kEdge, layout.top_value_y, kEdge + 15,
                     layout.top_value_y + 13,
                     s_corner_red);
    s_draw->drawLine(kEdge + 15, layout.top_value_y, kEdge,
                     layout.top_value_y + 13,
                     s_corner_red);
  }

  char value[12];
  applyCornerValueStyle();
  s_draw->setTextDatum(textdatum_t::top_right);
  s_draw->setTextColor(s_corner_green);
  snprintf(value, sizeof(value), "%u",
           static_cast<unsigned>(services::adsb::aircraftCount()));
  s_draw->drawString(value, kRight, layout.top_value_y);

  s_draw->setFont(&radar_fonts::Font0);
  s_draw->setTextSize(1);
  s_draw->setTextColor(s_corner_muted);
  s_draw->setTextDatum(textdatum_t::top_right);
  s_draw->drawString("AIR", kRight, layout.top_label_y);
  s_draw->setTextDatum(textdatum_t::top_left);
  s_draw->drawString("RANGE", kEdge, layout.bottom_label_y);
  s_draw->setTextDatum(textdatum_t::top_right);
  s_draw->drawString("TIME", kRight, layout.bottom_label_y);

  applyCornerValueStyle();
  s_draw->setTextDatum(textdatum_t::bottom_left);
  s_draw->setTextColor(s_corner_amber);
  radar::formatCurrentRing3Label(value, sizeof(value));
  s_draw->drawString(value, kEdge, layout.bottom_value_y);

  drawCornerTime(layout, value_height, kRight, false);
#endif
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  displayFontEnsureLoaded(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  initPalette();
  runway::drawLargeAirportRunways(gfx);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
#ifndef BOARD_NM_TV_154
  drawScaleLabel(cx, cy, grid_r);
#endif
  gfx.setTextDatum(textdatum_t::top_left);
}

void drawSweep(unsigned long now_ms) {
  if (!radar::showSweep()) {
    return;
  }

  constexpr float kTwoPi = 6.28318530718f;
  constexpr float kTrailStepRad = 0.06981317008f;
  const float angle =
      static_cast<float>(radar::sweepPhaseMs(now_ms)) /
      static_cast<float>(radar::kSweepPeriodMs) * kTwoPi;
  const uint16_t colors[] = {s_sweep_bright, s_sweep_mid, s_sweep_dim};

  for (size_t i = 0; i < 3; ++i) {
    const float ray = angle - static_cast<float>(i) * kTrailStepRad;
    const int x = radar::kCenterX + static_cast<int>(
                                           lroundf(sinf(ray) * radar::kGridOuterRadius));
    const int y = radar::kCenterY - static_cast<int>(
                                           lroundf(cosf(ray) * radar::kGridOuterRadius));
    s_draw->drawLine(radar::kCenterX, radar::kCenterY, x, y, colors[i]);
  }
}

bool ensureFrameSprite() {
#ifdef BOARD_NM_TV_154
  constexpr bool kFrameSpriteEnabled =
      radar::frameSpriteEnabledForBoard(true);
  constexpr bool kFrameSpriteNmTv154 = true;
#else
  constexpr bool kFrameSpriteEnabled =
      radar::frameSpriteEnabledForBoard(false);
  constexpr bool kFrameSpriteNmTv154 = false;
#endif
  if (!kFrameSpriteEnabled) {
    return false;
  }
  if (s_frame_ready) {
    return true;
  }
  if (s_frame_attempted) {
    return false;
  }
  s_frame_attempted = true;
  s_frame.setColorDepth(
      radar::frameSpriteColorDepthForBoard(kFrameSpriteNmTv154));
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    Serial.println("radar: frame sprite alloc failed");
    return false;
  }
  s_frame_ready = true;
#ifdef BOARD_NM_TV_154
  Serial.println("radar: frame sprite 8-bit ready");
#endif
  return true;
}

// Double-buffered frame: composite the grid AND aircraft into the off-screen
// sprite, then blit it to the panel in a single pushSprite. Because the panel
// is updated in one pass, labels never show an erase/redraw gap — no flicker.
void renderFrame(unsigned long now_ms) {
  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  {
    const DrawScope scope(s_frame);
    drawSweep(now_ms);
    drawCornerTelemetry();
    drawAircraft(now_ms);
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();
  const unsigned long now_ms = millis();

  if (ensureFrameSprite()) {
    const unsigned long draw_started_ms = millis();
    renderFrame(now_ms);
    recordFrameDuration(millis() - draw_started_ms);
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawSweep(now_ms);
  drawCornerTelemetry();
  drawAircraft(now_ms);
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshAircraft() {
  initPalette();
  const unsigned long now_ms = millis();

  if (ensureFrameSprite()) {
    const unsigned long draw_started_ms = millis();
    renderFrame(now_ms);
    recordFrameDuration(millis() - draw_started_ms);
    return;
  }

  radarDisplayDraw();
}

void radarDisplayRefreshStatus() {
#ifdef BOARD_NM_TV_154
  if (s_frame_ready) {
    return;
  }

  initPalette();
  initLabelMetrics();
  const DrawScope scope(tft);
  constexpr int kEdge = 5;
  constexpr int kValueGap = 2;
  const int right = radar::kSize - kEdge;

  tft.setFont(&radar_fonts::Font0);
  tft.setTextSize(1);
  const int label_height = tft.fontHeight();
  applyCornerValueStyle();
  const int value_height = tft.fontHeight();
  const nm_tv_154::CornerTelemetryLayout layout =
      nm_tv_154::cornerTelemetryLayout(radar::kSize, kEdge, label_height,
                                       value_height, kValueGap);
  drawCornerTime(layout, value_height, right, true);
  tft.setTextDatum(textdatum_t::top_left);
#endif
}

void radarDisplayMarkDataUpdated(unsigned long now_ms) {
#ifdef BOARD_NM_TV_154
  s_last_data_update_ms = now_ms;
  s_has_data_update = true;
#else
  (void)now_ms;
#endif
}

}  // namespace ui
