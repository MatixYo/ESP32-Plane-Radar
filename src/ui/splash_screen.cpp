#include "ui/splash_screen.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "hardware/display.h"

namespace ui {
namespace {

// --- Timing: hold + fade = about 3 seconds in total ---
constexpr unsigned long kHoldMs = 3300;
constexpr unsigned long kFadeMs = 700;

// --- Text: one entry per line, centred as a block in the heart. Add or remove
//     lines freely (up to about 4); keep each line to ~10 characters at size
//     1.15, or lower kTextSize for longer lines / more lines. ---
constexpr const char* kLines[] = {
    "",
    "For My Stinky",
    "",
    "<3 Danny",
};
constexpr size_t kLineCount = sizeof(kLines) / sizeof(kLines[0]);
constexpr float kTextSize = 1.15f;

// --- Colours (logical RGB; shade() applies the same R/B swap as the radar's red
//     aircraft). If the heart ever shows blue, set kSwapRedBlue to false. ---
constexpr bool kSwapRedBlue = config::kDisplayRgbOrder;
constexpr uint8_t kFillR = 230, kFillG = 0, kFillB = 0;
constexpr uint8_t kEdgeR = 255, kEdgeG = 120, kEdgeB = 120;
constexpr uint8_t kTextR = 255, kTextG = 255, kTextB = 255;

// --- Geometry: heart curve (x^2+y^2-1)^3 = x^2*y^3, 85 px per unit, drawn into
//     a sprite placed so the heart fits inside the 240 px round panel. ---
constexpr int kSpriteW = 200;
constexpr int kSpriteH = 196;
constexpr int kSpriteX = 20;  // sprite position on the screen
constexpr int kSpriteY = 32;
constexpr float kOriginX = 100.0f;  // heart-curve (0,0) in sprite pixels
constexpr float kOriginY = 108.0f;
constexpr float kScale = 85.0f;
constexpr float kInnerScale = kScale * 0.93f;  // inner fill; the rim is the outline
constexpr int kTextX = 100;        // text centre in sprite pixels
constexpr float kTextCentreY = 67.0f;  // vertical centre of the text block

struct RowSpans {
  int16_t a0, b0;  // first filled run (inclusive), -1 = none
  int16_t a1, b1;  // second run (rows through the cleft have two)
};

void computeSpans(RowSpans* out, float scale) {
  for (int py = 0; py < kSpriteH; ++py) {
    const float y = (kOriginY - static_cast<float>(py)) / scale;
    const float y2 = y * y;
    RowSpans row = {-1, -1, -1, -1};
    int runs = 0;
    int start = -1;
    for (int px = 0; px <= kSpriteW; ++px) {
      bool in = false;
      if (px < kSpriteW) {
        const float x = (static_cast<float>(px) - kOriginX) / scale;
        const float x2 = x * x;
        const float a = x2 + y2 - 1.0f;
        in = (a * a * a - x2 * y2 * y) <= 0.0f;
      }
      if (in && start < 0) {
        start = px;
      } else if (!in && start >= 0) {
        if (runs == 0) {
          row.a0 = static_cast<int16_t>(start);
          row.b0 = static_cast<int16_t>(px - 1);
        } else if (runs == 1) {
          row.a1 = static_cast<int16_t>(start);
          row.b1 = static_cast<int16_t>(px - 1);
        }
        ++runs;
        start = -1;
      }
    }
    out[py] = row;
  }
}

void fillSpans(LGFX_Sprite& spr, const RowSpans* spans, uint16_t color) {
  for (int y = 0; y < kSpriteH; ++y) {
    if (spans[y].a0 >= 0) {
      spr.drawFastHLine(spans[y].a0, y, spans[y].b0 - spans[y].a0 + 1, color);
    }
    if (spans[y].a1 >= 0) {
      spr.drawFastHLine(spans[y].a1, y, spans[y].b1 - spans[y].a1 + 1, color);
    }
  }
}

uint16_t shade(uint8_t r, uint8_t g, uint8_t b, float k) {
  const uint8_t rk = static_cast<uint8_t>(r * k);
  const uint8_t gk = static_cast<uint8_t>(g * k);
  const uint8_t bk = static_cast<uint8_t>(b * k);
  return kSwapRedBlue ? tft.color565(bk, gk, rk) : tft.color565(rk, gk, bk);
}

/** Compose the whole frame off-screen, then push it in one pass (no flicker). */
void renderFrame(LGFX_Sprite& spr, const RowSpans* outer, const RowSpans* inner,
                 float k) {
  spr.fillSprite(0);
  fillSpans(spr, outer, shade(kEdgeR, kEdgeG, kEdgeB, k));
  fillSpans(spr, inner, shade(kFillR, kFillG, kFillB, k));

  spr.setFont(&fonts::FreeSansBold12pt7b);
  spr.setTextSize(kTextSize);
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setTextColor(shade(kTextR, kTextG, kTextB, k));
  const int line_h = spr.fontHeight();
  const float first_y =
      kTextCentreY - static_cast<float>(kLineCount - 1) * static_cast<float>(line_h) * 0.5f;
  for (size_t i = 0; i < kLineCount; ++i) {
    spr.drawString(kLines[i], kTextX,
                   static_cast<int>(first_y + static_cast<float>(i * line_h)));
  }

  spr.pushSprite(&tft, kSpriteX, kSpriteY);
}

}  // namespace

void splashScreenShow() {
  const unsigned long t0 = millis();
  tft.fillScreen(0);

  // Same 1-byte sprite type as the radar frame buffer.
  LGFX_Sprite spr(&tft);
  spr.setColorDepth(lgfx::color_depth_t::rgb332_1Byte);
  if (!spr.createSprite(kSpriteW, kSpriteH)) {
    Serial.println("splash: sprite alloc failed");
    while (millis() - t0 < kHoldMs + kFadeMs) {
      delay(10);
    }
    return;
  }

  RowSpans outer[kSpriteH];
  RowSpans inner[kSpriteH];
  computeSpans(outer, kScale);
  computeSpans(inner, kInnerScale);

  renderFrame(spr, outer, inner, 1.0f);
  while (millis() - t0 < kHoldMs) {
    delay(10);
  }

  const unsigned long fade_start = millis();
  for (;;) {
    const unsigned long elapsed = millis() - fade_start;
    if (elapsed >= kFadeMs) {
      break;
    }
    const float k = 1.0f - static_cast<float>(elapsed) / static_cast<float>(kFadeMs);
    renderFrame(spr, outer, inner, k);
  }

  spr.deleteSprite();
  tft.fillScreen(0);
}

}  // namespace ui