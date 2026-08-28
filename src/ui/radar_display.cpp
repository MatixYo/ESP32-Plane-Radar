#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/track_history.h"
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
uint16_t kColorVertClimb = 0x07E0;
uint16_t kColorVertDescent = 0xFD20;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorTagRoute = 0x9772;
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

// Take-turns animation for overlapping tags: set by drawAircraft() each render,
// read by radarDisplayAnimTick() to decide when the next turn is due.
bool s_tag_cycle_active = false;
uint32_t s_tag_cycle_phase_drawn = 0;

lgfx::LovyanGFX* s_draw = &tft;
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
  if (config::kDisplayRgbOrder) {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR);
  } else {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB);
  }
  // BGR panel: swap R/B so the trail renders true yellow (not cyan).
  if (config::kDisplayRgbOrder) {
    radar::kColorTrackVector =
        tft.color565(radar::kTrackB, radar::kTrackG, radar::kTrackR);
  } else {
    radar::kColorTrackVector =
        tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
  }
  // BGR panel: swap R/B so the climb/descent triangle renders true green/amber.
  if (config::kDisplayRgbOrder) {
    radar::kColorVertClimb =
        tft.color565(radar::kVertClimbB, radar::kVertClimbG, radar::kVertClimbR);
    radar::kColorVertDescent = tft.color565(
        radar::kVertDescentB, radar::kVertDescentG, radar::kVertDescentR);
  } else {
    radar::kColorVertClimb =
        tft.color565(radar::kVertClimbR, radar::kVertClimbG, radar::kVertClimbB);
    radar::kColorVertDescent = tft.color565(
        radar::kVertDescentR, radar::kVertDescentG, radar::kVertDescentB);
  }
  radar::kColorTagType =
      tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
  radar::kColorTagAltitude =
      tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  radar::kColorTagRoute =
      tft.color565(radar::kTagRouteR, radar::kTagRouteG, radar::kTagRouteB);
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

// True when the vertical rate is worth showing an arrow for (level cruise is
// left unmarked so the tag stays quiet).
bool showsVertRate(float vert_rate_fpm) {
  return fabsf(vert_rate_fpm) >= radar::kVertRateThresholdFpm;
}

// Horizontal span the inline arrow + its trailing gap reserve on the altitude
// tag line, ahead of the altitude text.
int vertRateArrowSpanPx() {
  return radar::kVertRateGlyphHalfWpx * 2 + radar::kVertRateGlyphGapPx;
}

// Small filled triangle centred at (cx, cy): points up for a climb, down for a
// descent. Drawn on the altitude tag line, just before the altitude value.
void drawVertRateArrow(int cx, int cy, float vert_rate_fpm) {
  const int hw = radar::kVertRateGlyphHalfWpx;
  const int hh = radar::kVertRateGlyphHpx / 2;
  if (vert_rate_fpm > 0.0f) {
    s_draw->fillTriangle(cx, cy - hh, cx - hw, cy + hh, cx + hw, cy + hh,
                         radar::kColorVertClimb);
  } else {
    s_draw->fillTriangle(cx, cy + hh, cx - hw, cy - hh, cx + hw, cy - hh,
                         radar::kColorVertDescent);
  }
}

// Blend `color` toward the radar background: frac 1 = full colour, 0 = invisible.
uint16_t fadeToBackground(uint16_t color, float frac) {
  if (frac >= 1.0f) {
    return color;
  }
  if (frac <= 0.0f) {
    return radar::kColorBackground;
  }
  const auto lerp = [frac](int from, int to) {
    return static_cast<int>(from + (to - from) * frac + 0.5f);
  };
  const int bg_r = (radar::kColorBackground >> 11) & 0x1F;
  const int bg_g = (radar::kColorBackground >> 5) & 0x3F;
  const int bg_b = radar::kColorBackground & 0x1F;
  const int r = lerp(bg_r, (color >> 11) & 0x1F);
  const int g = lerp(bg_g, (color >> 5) & 0x3F);
  const int b = lerp(bg_b, color & 0x1F);
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Breadcrumb trail: the aircraft's recent fixes, re-projected to the active
// range scale and joined newest-segment-brightest, fading toward the oldest.
// Stops early if the trail would leave the outer ring. Replaces the old
// predicted-heading vector.
void drawTrackPath(const char* hex, int cur_x, int cur_y) {
  const services::track::Point* pts = nullptr;
  const size_t count = services::track::path(hex, &pts);
  if (count == 0) {
    return;
  }

  const int max_r_sq = radar::kGridOuterRadius * radar::kGridOuterRadius;
  int prev_x = cur_x;
  int prev_y = cur_y;
  for (size_t k = 0; k < count; ++k) {
    const size_t idx = count - 1 - k;  // pts are oldest -> newest
    int x = 0;
    int y = 0;
    latLonToScreen(pts[idx].lat, pts[idx].lon, &x, &y);
    if (distSqFromCenter(x, y) > max_r_sq) {
      break;
    }
    // k = 0 is the freshest segment (current position -> last fix).
    const float age = static_cast<float>(k) / static_cast<float>(count);
    const float frac = 0.25f + 0.75f * (1.0f - age);
    s_draw->drawWideLine(prev_x, prev_y, x, y,
                         radar::kAircraftTrackLineHalfWidth,
                         fadeToBackground(radar::kColorTrackVector, frac));
    prev_x = x;
    prev_y = y;
  }
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
  }
}

// "Londra>New York" from origin/dest; "" when neither endpoint is known.
// A missing side is shown as "?" so a one-sided route still reads as a route.
void formatRouteLine(const services::adsb::Aircraft& plane, char* out,
                     size_t out_len) {
  out[0] = '\0';
  if (out_len == 0 || (plane.origin[0] == '\0' && plane.dest[0] == '\0')) {
    return;
  }
  const char* from = plane.origin[0] != '\0' ? plane.origin : "?";
  const char* to = plane.dest[0] != '\0' ? plane.dest : "?";
  snprintf(out, out_len, "%s>%s", from, to);
}

// "A320 Ryanair" from type + airline; either half alone when the other is
// missing; "" when both are.
void formatTypeLine(const services::adsb::Aircraft& plane, char* out,
                    size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }
  const bool has_type = plane.type[0] != '\0';
  const bool has_airline = plane.airline[0] != '\0';
  if (has_type && has_airline) {
    snprintf(out, out_len, "%s %s", plane.type, plane.airline);
  } else if (has_type) {
    snprintf(out, out_len, "%s", plane.type);
  } else if (has_airline) {
    snprintf(out, out_len, "%s", plane.airline);
  }
}

int measureTagBlockWidth(const services::adsb::Aircraft& plane,
                         const char* type_line, const char* route_line) {
  applyTagStyle();
  int max_w = 0;
  if (plane.callsign[0] != '\0') {
    const int w = s_draw->textWidth(plane.callsign);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (type_line[0] != '\0') {
    const int w = s_draw->textWidth(type_line);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.alt[0] != '\0') {
    int w = s_draw->textWidth(plane.alt);
    if (showsVertRate(plane.vert_rate_fpm)) {
      w += vertRateArrowSpanPx();  // inline climb/descent arrow before the value
    }
    if (w > max_w) {
      max_w = w;
    }
  }
  if (route_line[0] != '\0') {
    const int w = s_draw->textWidth(route_line);
    if (w > max_w) {
      max_w = w;
    }
  }
  return max_w;
}

// Everything needed to both place a tag and test it for overlap. Built once
// per render so the layout math is not repeated for the box test and the draw.
struct TagRender {
  int anchor_x = 0;
  int ly = 0;
  int line_h = 0;
  textdatum_t datum = textdatum_t::top_left;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // screen bounding box of the block
  char type_line[32] = {0};
  char route_line[40] = {0};
};

TagRender buildTag(int x, int y, const services::adsb::Aircraft& plane) {
  initTagLabelMetrics();
  applyTagStyle();

  TagRender t;
  formatTypeLine(plane, t.type_line, sizeof(t.type_line));
  formatRouteLine(plane, t.route_line, sizeof(t.route_line));

  t.line_h = s_draw->fontHeight();
  const int block_w = measureTagBlockWidth(plane, t.type_line, t.route_line);
  const int line_count = t.route_line[0] != '\0' ? 4 : 3;
  const int block_h = t.line_h * line_count;
  int ly = y - block_h / 2;

  const int symbol_half =
      radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx;
  // West (left): tag toward center on the right; east (right): tag on the left.
  const bool tag_on_right = x < radar::kCenterX;
  if (tag_on_right) {
    t.anchor_x = std::min(x + symbol_half + radar::kAircraftLabelGapPx,
                          radar::kSize - block_w - 1);
    t.datum = textdatum_t::top_left;
    t.x0 = t.anchor_x;
    t.x1 = t.anchor_x + block_w;
  } else {
    t.anchor_x = std::max(x - symbol_half - radar::kAircraftLabelGapPx,
                          block_w + 1);
    t.datum = textdatum_t::top_right;
    t.x0 = t.anchor_x - block_w;
    t.x1 = t.anchor_x;
  }
  ly = std::max(1, std::min(ly, radar::kSize - block_h - 1));
  t.ly = ly;
  t.y0 = ly;
  t.y1 = ly + block_h;
  return t;
}

bool tagsOverlap(const TagRender& a, const TagRender& b) {
  const int p = radar::kAircraftTagOverlapPadPx;
  return a.x0 - p < b.x1 + p && a.x1 + p > b.x0 - p && a.y0 - p < b.y1 + p &&
         a.y1 + p > b.y0 - p;
}

void drawTag(const TagRender& t, const services::adsb::Aircraft& plane) {
  applyTagStyle();
  s_draw->setTextDatum(t.datum);
  int ly = t.ly;

  if (plane.callsign[0] != '\0') {
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    s_draw->drawString(plane.callsign, t.anchor_x, ly);
  }
  ly += t.line_h;

  if (t.type_line[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
    s_draw->drawString(t.type_line, t.anchor_x, ly);
  }
  ly += t.line_h;

  if (plane.alt[0] != '\0') {
    const bool show_rate = showsVertRate(plane.vert_rate_fpm);
    const bool text_flows_right = t.datum == textdatum_t::top_left;
    // Left-flowing text stays at anchor_x (the arrow falls to its left, inside
    // the block); right-flowing text is pushed in to open room before it.
    const int text_x = (show_rate && text_flows_right)
                           ? t.anchor_x + vertRateArrowSpanPx()
                           : t.anchor_x;
    s_draw->setTextColor(radar::kColorTagAltitude, radar::kColorBackground);
    s_draw->drawString(plane.alt, text_x, ly);
    if (show_rate) {
      const int alt_w = s_draw->textWidth(plane.alt);
      const int text_left = text_flows_right ? text_x : t.anchor_x - alt_w;
      const int arrow_cx =
          text_left - radar::kVertRateGlyphGapPx - radar::kVertRateGlyphHalfWpx;
      drawVertRateArrow(arrow_cx, ly + t.line_h / 2, plane.vert_rate_fpm);
    }
  }
  ly += t.line_h;

  if (t.route_line[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagRoute, radar::kColorBackground);
    s_draw->drawString(t.route_line, t.anchor_x, ly);
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
  s_tag_cycle_active = false;

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

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
    if (radar::showTrail()) {
      drawTrackPath(planes[i].hex, x, y);
    }
    drawHeadingTriangle(x, y, planes[i].nose_deg, radar::kColorAircraft);
  }

  // Lay out every tag, then group tags whose blocks overlap (transitively) into
  // clusters. A lone tag is always drawn; within a cluster only one member is
  // shown at a time, advancing every kAircraftTagCycleMs so stacked labels take
  // turns instead of painting over each other. Buffers are static (drawAircraft
  // is not reentrant) to keep this off the small loop-task stack.
  static TagRender tags[services::adsb::kMaxAircraft];
  static int cluster[services::adsb::kMaxAircraft];
  static int cluster_size[services::adsb::kMaxAircraft];
  static size_t bfs_stack[services::adsb::kMaxAircraft];

  for (size_t d = 0; d < draw_count; ++d) {
    tags[d] = buildTag(items[d].x, items[d].y, planes[items[d].index]);
    cluster[d] = -1;
    cluster_size[d] = 0;
  }

  int cluster_count = 0;
  for (size_t seed = 0; seed < draw_count; ++seed) {
    if (cluster[seed] != -1) {
      continue;
    }
    size_t sp = 0;
    bfs_stack[sp++] = seed;
    cluster[seed] = cluster_count;
    while (sp > 0) {
      const size_t a = bfs_stack[--sp];
      for (size_t b = 0; b < draw_count; ++b) {
        if (cluster[b] == -1 && tagsOverlap(tags[a], tags[b])) {
          cluster[b] = cluster_count;
          bfs_stack[sp++] = b;
        }
      }
    }
    ++cluster_count;
  }

  for (size_t d = 0; d < draw_count; ++d) {
    ++cluster_size[cluster[d]];
  }

  const uint32_t phase = millis() / radar::kAircraftTagCycleMs;
  for (size_t d = 0; d < draw_count; ++d) {
    const int c = cluster[d];
    if (cluster_size[c] <= 1) {
      drawTag(tags[d], planes[items[d].index]);
      continue;
    }
    s_tag_cycle_active = true;
    int pos = 0;  // stable index of this tag within its cluster
    for (size_t e = 0; e < d; ++e) {
      if (cluster[e] == c) {
        ++pos;
      }
    }
    if (static_cast<int>(phase % static_cast<uint32_t>(cluster_size[c])) == pos) {
      drawTag(tags[d], planes[items[d].index]);
    }
  }

  s_tag_cycle_phase_drawn = phase;

  static bool s_was_cycling = false;
  if (s_tag_cycle_active != s_was_cycling) {
    s_was_cycling = s_tag_cycle_active;
    Serial.printf("tags: %s\n", s_tag_cycle_active
                                    ? "overlap — labels taking turns"
                                    : "no overlap — all labels shown");
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

// Double-buffered frame: composite the grid AND aircraft into the off-screen
// sprite, then blit it to the panel in a single pushSprite. Because the panel
// is updated in one pass, labels never show an erase/redraw gap — no flicker.
void renderFrame() {
  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  {
    const DrawScope scope(s_frame);
    drawAircraft();
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawAircraft();
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshAircraft() {
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  radarDisplayDraw();
}

void radarDisplayAnimTick() {
  if (!s_tag_cycle_active) {
    return;  // nothing is stacked — nothing to animate
  }
  if (millis() / radar::kAircraftTagCycleMs == s_tag_cycle_phase_drawn) {
    return;  // current turn still has time left
  }
  radarDisplayRefreshAircraft();  // repaints with the next cluster member shown
}

}  // namespace ui
