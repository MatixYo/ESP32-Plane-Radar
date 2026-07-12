#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/radar_location.h"
#include "services/satellite_tracker.h"
#include "ui/radar_theme.h"

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
float s_cardinal_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_tag_gfx = &fonts::FreeSansBold12pt7b;

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
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;
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
  if (config::kDisplayRgbOrder) {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR);
  } else {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB);
  }
  radar::kColorTagAltitude =
      tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
}

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

/** 0° elevation = outer ring (horizon), 90° = center (zenith). Azimuth: 0=N, clockwise. */
void azElToScreen(float az_deg, float el_deg, int* out_x, int* out_y) {
  const float clamped_el = std::max(0.0f, std::min(90.0f, el_deg));
  const float r = radar::kGridOuterRadius * (1.0f - clamped_el / 90.0f);
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = az_deg * kDegToRad;
  *out_x = radar::kCenterX + static_cast<int>(lroundf(sinf(rad) * r));
  *out_y = radar::kCenterY - static_cast<int>(lroundf(cosf(rad) * r));
}

void drawSatelliteDot(int x, int y) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx + 2,
                           radar::kColorAircraft);
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_tag_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
  }
}

int measureSatTagWidth(const services::satellites::Satellite& sat,
                      char* el_text, size_t el_text_len) {
  applyTagStyle();
  snprintf(el_text, el_text_len, "%d deg", static_cast<int>(lroundf(sat.elevation_deg)));
  int max_w = s_draw->textWidth(sat.name);
  const int w2 = s_draw->textWidth(el_text);
  if (w2 > max_w) {
    max_w = w2;
  }
  return max_w;
}

void drawSatelliteTag(int x, int y, const services::satellites::Satellite& sat) {
  initTagLabelMetrics();
  applyTagStyle();

  char el_text[12];
  const int line_h = s_draw->fontHeight();
  const int block_w = measureSatTagWidth(sat, el_text, sizeof(el_text));
  const int block_h = line_h * 2;
  int ly = y - block_h / 2;

  const int symbol_half = radar::kBeyondRingDotRadiusPx + 2;
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

  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(sat.name, anchor_x, ly);
  ly += line_h;

  s_draw->setTextColor(radar::kColorTagAltitude, radar::kColorBackground);
  s_draw->drawString(el_text, anchor_x, ly);
}

struct SatDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

void sortSatItemsFarFirst(SatDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const SatDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void drawSatellites() {
  initLabelMetrics();

  const size_t n = services::satellites::visibleCount();
  const services::satellites::Satellite* sats = services::satellites::visibleList();

  SatDrawItem items[services::satellites::kMaxVisible];
  size_t count = 0;

  for (size_t i = 0; i < n; ++i) {
    int x = 0;
    int y = 0;
    azElToScreen(sats[i].azimuth_deg, sats[i].elevation_deg, &x, &y);
    items[count].index = i;
    items[count].x = x;
    items[count].y = y;
    items[count].dist_sq = distSqFromCenter(x, y);
    ++count;
  }

  // Draw satellites near the horizon first, so ones near zenith (usually the
  // most interesting) end up drawn on top.
  sortSatItemsFarFirst(items, count);

  for (size_t d = 0; d < count; ++d) {
    drawSatelliteDot(items[d].x, items[d].y);
  }
  for (size_t d = 0; d < count; ++d) {
    drawSatelliteTag(items[d].x, items[d].y, sats[items[d].index]);
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw, s_cardinal_vlw_size);
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
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
  drawCenterDot(cx, cy);
  drawCardinalLabels();
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

void renderFrame() {
  drawStaticGrid(s_frame);
  {
    const DrawScope scope(s_frame);
    drawSatellites();
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

  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawSatellites();
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshSatellites() {
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  radarDisplayDraw();
}

}  // namespace ui
