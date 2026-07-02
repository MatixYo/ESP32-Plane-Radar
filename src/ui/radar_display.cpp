#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "config.h"
#include "hardware/board.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

namespace fonts = lgfx::v1::fonts;

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
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_tag_gfx = &fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

lgfx::LovyanGFX* s_draw = &tft;
// Single 16-bit compositing buffer: grid + aircraft + sweep are drawn here every
// frame and pushed to the panel in one pass (flicker-free). A second cache
// buffer won't fit alongside the TLS connection on this chip, so the grid is
// re-rendered each frame; doing it every frame keeps the cadence uniform.
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

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

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&fonts::FreeSansBold9pt7b,
                                               &fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
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

  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {&fonts::FreeSansBold12pt7b,
                                               &fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }

  s_tag_label_metrics_ready = true;
}

void initPalette() {
  radar::kColorBackground = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
  radar::kColorGrid = tft.color565(radar::kGridR, radar::kGridG, radar::kGridB);
  radar::kColorLabel = tft.color565(255, 255, 255);
  radar::kColorCenter = tft.color565(255, 255, 255);
  // GC9A01 BGR panel: swap R/B in color565 so logical red renders red on screen.
  if (hardware::board::activePins().rgb_order) {
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
  }
}

struct TagLine {
  const char* text;
  uint16_t color;
};

constexpr int kMaxTagLines = 4;

// Builds the tag lines for an aircraft into out_lines. The airline line is shown
// (full name or friendly abbreviation) only when enabled and resolved.
int buildTagLines(const services::adsb::Aircraft& plane, TagLine* out_lines) {
  int n = 0;
  const ui::radar::AirlineDisplay mode = ui::radar::airlineDisplay();
  if (mode != ui::radar::AirlineDisplay::kNone && plane.airline != nullptr) {
    const char* text = (mode == ui::radar::AirlineDisplay::kAbbrev)
                           ? plane.airline->short_name
                           : plane.airline->name;
    if (text != nullptr && text[0] != '\0') {
      out_lines[n++] = {text, radar::kColorLabel};
    }
  }
  if (plane.callsign[0] != '\0') {
    out_lines[n++] = {plane.callsign, radar::kColorLabel};
  }
  if (plane.type[0] != '\0') {
    out_lines[n++] = {plane.type, radar::kColorTagType};
  }
  if (plane.alt[0] != '\0') {
    out_lines[n++] = {plane.alt, radar::kColorTagAltitude};
  }
  return n;
}

void drawAircraftTag(int x, int y, const services::adsb::Aircraft& plane) {
  initTagLabelMetrics();
  applyTagStyle();

  TagLine lines[kMaxTagLines];
  const int n_lines = buildTagLines(plane, lines);
  if (n_lines == 0) {
    return;
  }

  const int line_h = s_draw->fontHeight();
  int block_w = 0;
  for (int i = 0; i < n_lines; ++i) {
    const int w = s_draw->textWidth(lines[i].text);
    if (w > block_w) {
      block_w = w;
    }
  }
  const int block_h = line_h * n_lines;
  int ly = y - block_h / 2;

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  // West (left): tag toward center on the right; east (right): tag on the left.
  const bool tag_on_right = x < radar::kCenterX;
  int anchor_x = 0;
  if (tag_on_right) {
    anchor_x = x + symbol_half + radar::kAircraftLabelGapPx;
    anchor_x = std::min(anchor_x, radar::kSize - block_w - 1);
    s_draw->setTextDatum(textdatum_t::top_left);
  } else {
    anchor_x = x - symbol_half - radar::kAircraftLabelGapPx;
    anchor_x = std::max(anchor_x, block_w + 1);
    s_draw->setTextDatum(textdatum_t::top_right);
  }
  ly = std::max(1, std::min(ly, radar::kSize - block_h - 1));

  for (int i = 0; i < n_lines; ++i) {
    s_draw->setTextColor(lines[i].color, radar::kColorBackground);
    s_draw->drawString(lines[i].text, anchor_x, ly);
    ly += line_h;
  }
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct BeyondDotDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

// Screen positions of aircraft from the last draw, for touch hit-testing.
struct HitTarget {
  size_t index;
  int x;
  int y;
};
HitTarget s_hit_targets[services::adsb::kMaxAircraft];
size_t s_hit_count = 0;

// --- Radar sweep animation ---
// A green wedge, full intensity at the leading edge fading to 0 over the span,
// rotating clockwise. Colour #8BF688.
constexpr int kSweepR = 0x8B;
constexpr int kSweepG = 0xF6;
constexpr int kSweepB = 0x88;
constexpr float kSweepSpanDeg = 25.0f;   // bright front -> 0 over this many deg
constexpr float kSweepStepDeg = 0.5f;    // radial line spacing
constexpr float kSweepDegPerSec = 90.0f; // rotation speed (~4 s per revolution)
constexpr unsigned long kSweepFrameMs = 50;  // ~20 fps cap

float s_sweep_deg = 0.0f;
unsigned long s_last_sweep_ms = 0;

uint16_t sweepColorForIntensity(float t) {
  const int r = radar::kBgR + static_cast<int>(lroundf((kSweepR - radar::kBgR) * t));
  const int g = radar::kBgG + static_cast<int>(lroundf((kSweepG - radar::kBgG) * t));
  const int b = radar::kBgB + static_cast<int>(lroundf((kSweepB - radar::kBgB) * t));
  // Match the panel's colour order (same rule as the aircraft colour).
  if (hardware::board::activePins().rgb_order) {
    return tft.color565(b, g, r);
  }
  return tft.color565(r, g, b);
}

// Draw the sweep into the current draw target (the compositing buffer).
void drawSweep(float lead_deg) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int radius = radar::kGridOuterRadius;
  constexpr float kDegToRad = 0.0174532925f;
  // Far (faint) to near (bright) so the bright leading edge is drawn last.
  for (float a = kSweepSpanDeg; a >= 0.0f; a -= kSweepStepDeg) {
    const float t = 1.0f - a / kSweepSpanDeg;  // 0 trailing, 1 leading
    const uint16_t color = sweepColorForIntensity(t);
    const float rad = (lead_deg - a) * kDegToRad;
    const int ex = cx + static_cast<int>(lroundf(sinf(rad) * radius));
    const int ey = cy - static_cast<int>(lroundf(cosf(rad) * radius));
    s_draw->drawLine(cx, cy, ex, ey, color);
  }
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

void drawAircraft() {
  initLabelMetrics();

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[services::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;
  s_hit_count = 0;

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
      s_hit_targets[s_hit_count++] = {i, x, y};
      continue;
    }

    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(planes[i].lat, planes[i].lon, &dot_x,
                                     &dot_y)) {
      continue;
    }
    dots[dot_count].index = i;
    dots[dot_count].x = dot_x;
    dots[dot_count].y = dot_y;
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    ++dot_count;
    s_hit_targets[s_hit_count++] = {i, dot_x, dot_y};
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
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    drawAircraftTag(items[d].x, items[d].y, planes[i]);
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
  drawScaleLabel(cx, cy, grid_r);
  gfx.setTextDatum(textdatum_t::top_left);
}

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  s_frame.setColorDepth(16);
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    Serial.println("radar: frame sprite alloc failed");
    return false;
  }
  s_frame_ready = true;
  return true;
}

// Composite grid + aircraft (+ optional sweep) into s_frame and push once, so
// the panel updates in a single pass (no flicker).
void composeFrame(bool with_sweep) {
  if (!ensureFrameSprite()) {
    // No buffer: draw straight to the panel (may flicker, last resort).
    const DrawScope scope(tft);
    drawStaticGrid(tft);
    drawAircraft();
    if (with_sweep) {
      drawSweep(s_sweep_deg);
    }
    tft.setTextDatum(textdatum_t::top_left);
    return;
  }

  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  {
    const DrawScope scope(s_frame);
    drawAircraft();
    if (with_sweep) {
      drawSweep(s_sweep_deg);
    }
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();
  composeFrame(true);
}

void radarDisplayRefreshAircraft() {
  initPalette();
  composeFrame(true);
}

void radarDisplayAnimate() {
  const unsigned long now = millis();
  if (s_last_sweep_ms != 0 && now - s_last_sweep_ms < kSweepFrameMs) {
    return;  // frame-rate cap
  }
  const float dt =
      (s_last_sweep_ms == 0) ? 0.05f : (now - s_last_sweep_ms) / 1000.0f;
  s_last_sweep_ms = now;
  s_sweep_deg += kSweepDegPerSec * dt;
  while (s_sweep_deg >= 360.0f) {
    s_sweep_deg -= 360.0f;
  }

  // One composited, flicker-free frame: cached grid + aircraft + sweep.
  composeFrame(true);
}

int radarDisplayHitTest(int x, int y) {
  constexpr int kTapRadius = 26;
  constexpr int kTapRadiusSq = kTapRadius * kTapRadius;
  int best = -1;
  int best_sq = kTapRadiusSq + 1;
  for (size_t i = 0; i < s_hit_count; ++i) {
    const int dx = s_hit_targets[i].x - x;
    const int dy = s_hit_targets[i].y - y;
    const int d2 = dx * dx + dy * dy;
    if (d2 <= kTapRadiusSq && d2 < best_sq) {
      best_sq = d2;
      best = static_cast<int>(s_hit_targets[i].index);
    }
  }
  return best;
}

void radarDisplayDrawDialog(int aircraft_index,
                            const services::route::RouteInfo* route) {
  const size_t n = services::adsb::aircraftCount();
  if (aircraft_index < 0 || static_cast<size_t>(aircraft_index) >= n) {
    return;
  }
  const services::adsb::Aircraft& ac =
      services::adsb::aircraftList()[static_cast<size_t>(aircraft_index)];

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(ac.lat, ac.lon, &dx_km, &dy_km, &dist_km);

  // Title (callsign) plus one line per enabled detail field.
  char title[16];
  snprintf(title, sizeof(title), "%s",
           ac.callsign[0] != '\0' ? ac.callsign : "(no id)");

  char lines[10][28];
  int nl = 0;
  using ui::radar::DialogField;
  if (ui::radar::dialogFieldEnabled(DialogField::kAirline) &&
      ac.airline != nullptr) {
    snprintf(lines[nl++], sizeof(lines[0]), "%s", ac.airline->name);
  }
  if (ui::radar::dialogFieldEnabled(DialogField::kRoute) && route != nullptr &&
      route->valid) {
    if (route->origin_city[0] != '\0') {
      snprintf(lines[nl++], sizeof(lines[0]), "%s %s", route->origin_code,
               route->origin_city);
    } else {
      snprintf(lines[nl++], sizeof(lines[0]), "%s", route->origin_code);
    }
    if (route->dest_city[0] != '\0') {
      snprintf(lines[nl++], sizeof(lines[0]), "-> %s %s", route->dest_code,
               route->dest_city);
    } else {
      snprintf(lines[nl++], sizeof(lines[0]), "-> %s", route->dest_code);
    }
  }
  if (ui::radar::dialogFieldEnabled(DialogField::kType) && ac.type[0] != '\0') {
    snprintf(lines[nl++], sizeof(lines[0]), "Type: %s", ac.type);
  }
  if (ui::radar::dialogFieldEnabled(DialogField::kAltitude) &&
      ac.alt[0] != '\0') {
    snprintf(lines[nl++], sizeof(lines[0]), "Alt: %s", ac.alt);
  }
  if (ui::radar::dialogFieldEnabled(DialogField::kSpeed)) {
    snprintf(lines[nl++], sizeof(lines[0]), "Spd: %d kt",
             static_cast<int>(lroundf(ac.gs_knots)));
  }
  if (ui::radar::dialogFieldEnabled(DialogField::kTrack)) {
    snprintf(lines[nl++], sizeof(lines[0]), "Trk: %d deg",
             static_cast<int>(lroundf(ac.track_deg)));
  }
  if (ui::radar::dialogFieldEnabled(DialogField::kDistance)) {
    if (ui::radar::useMiles()) {
      snprintf(lines[nl++], sizeof(lines[0]), "Dist: %.1f mi",
               dist_km / 1.609344f);
    } else {
      snprintf(lines[nl++], sizeof(lines[0]), "Dist: %.1f km", dist_km);
    }
  }
  if (ui::radar::dialogFieldEnabled(DialogField::kPosition)) {
    snprintf(lines[nl++], sizeof(lines[0]), "%.3f, %.3f", ac.lat, ac.lon);
  }

  // Blank the screen to de-clutter, then draw the details centred.
  initTagLabelMetrics();
  initPalette();
  const DrawScope scope(tft);
  displayFontEnsureLoaded(tft);
  tft.fillScreen(radar::kColorBackground);
  tft.setTextDatum(textdatum_t::middle_center);

  const int cx = radar::kCenterX;

  const float scale = ui::radar::dialogTextScale();
  auto setTitleFont = [scale]() {
    if (displayFontIsSmooth()) {
      displayFontSetSmoothSize(tft, 1.6f * scale);
    } else {
      displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
    }
  };
  auto setBodyFont = [scale]() {
    if (displayFontIsSmooth()) {
      displayFontSetSmoothSize(tft, 1.1f * scale);
    } else {
      displayFontSetBitmap(tft, &fonts::FreeSansBold9pt7b);
    }
  };

  setTitleFont();
  const int title_h = tft.fontHeight();
  setBodyFont();
  const int body_h = tft.fontHeight();

  const int total_h = title_h + nl * body_h;
  int y = radar::kCenterY - total_h / 2 + title_h / 2;

  setTitleFont();
  tft.setTextColor(radar::kColorLabel, radar::kColorBackground);
  tft.drawString(title, cx, y);
  y += title_h / 2 + body_h / 2;

  setBodyFont();
  for (int i = 0; i < nl; ++i) {
    tft.setTextColor(radar::kColorLabel, radar::kColorBackground);
    tft.drawString(lines[i], cx, y);
    y += body_h;
  }

  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui
