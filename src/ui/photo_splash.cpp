#include "ui/photo_splash.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "ui/photo_splash_config.h"
#include "ui/splash_photos_data.h"

namespace ui {
namespace {

constexpr int kSize = 240;
constexpr float kPi = 3.14159265f;
constexpr float kHalfSpanRad = 75.0f * kPi / 180.0f;
constexpr int kOutline = 2;  // outline thickness in px

LGFX_Sprite s_text(&tft);  // caption rendered flat: 0 = nothing, 0xFF = letter, else outline
const uint8_t* s_buf = nullptr;
int s_text_w = 0;  // letters only (without outline margin)
int s_band_h = 0;  // strip height including outline margin
bool s_have_caption = false;

bool buildCaption() {
  using namespace photosplash;
  if (kCaption[0] == '\0') {
    return false;
  }
  int tw = 0, fh = 0;
  {
    LGFX_Sprite probe(&tft);
    probe.setColorDepth(lgfx::color_depth_t::rgb332_1Byte);
    if (!probe.createSprite(1, 1)) return false;
    probe.setFont(&fonts::FreeSansBold9pt7b);
    probe.setTextSize(kTextScale);
    tw = probe.textWidth(kCaption);
    fh = probe.fontHeight();
    probe.deleteSprite();
  }
  if (tw < 1 || fh < 8 || tw > 600) return false;

  const int w = tw + kOutline * 2;
  const int h = fh + kOutline * 2;
  s_text.setColorDepth(lgfx::color_depth_t::rgb332_1Byte);
  if (!s_text.createSprite(w, h)) return false;
  s_text.fillSprite(0);
  s_text.setFont(&fonts::FreeSansBold9pt7b);
  s_text.setTextSize(kTextScale);
  s_text.setTextDatum(textdatum_t::top_left);
  s_text.setTextColor(0x001F);  // stored as a dark non-zero value = outline
  for (int dy = -kOutline; dy <= kOutline; ++dy) {
    for (int dx = -kOutline; dx <= kOutline; ++dx) {
      if (dx == 0 && dy == 0) continue;
      s_text.drawString(kCaption, kOutline + dx, kOutline + dy);
    }
  }
  s_text.setTextColor(0xFFFF);  // letters: 0xFF in this 8-bit sprite
  s_text.drawString(kCaption, kOutline, kOutline);

  s_buf = static_cast<const uint8_t*>(s_text.getBuffer());
  s_text_w = w;
  s_band_h = h;
  return s_buf != nullptr;
}

/** Bend the flat caption onto the top edge, centred, upright, drawn straight onto the panel. */
void drawCaption() {
  using namespace photosplash;
  if (!s_have_caption) return;

  const float r_out = kSize * 0.5f - static_cast<float>(kEdgeMarginPx) + kOutline;
  const float r_in = r_out - static_cast<float>(s_band_h);
  const float r_mid = (r_out + r_in) * 0.5f;
  const float half_arc = r_mid * kHalfSpanRad;
  if (static_cast<float>(s_text_w) * 0.5f > half_arc) {
    // Too long to fit the arc: skip rather than run off the sides.
    return;
  }
  // Colour of the letters; same red/blue handling as the rest of the radar.
  const uint16_t ink = config::kDisplayRgbOrder ? tft.color565(kTextB, kTextG, kTextR)
                                                : tft.color565(kTextR, kTextG, kTextB);
  const uint16_t edge = tft.color565(0, 0, 0);

  const float cx = kSize * 0.5f;
  const float cy = kSize * 0.5f;
  const float u0 = static_cast<float>(s_text_w) * 0.5f;  // strip centre <-> straight up

  tft.startWrite();
  for (int y = 0; y < kSize / 2; ++y) {
    const float dy = cy - (static_cast<float>(y) + 0.5f);
    if (dy >= r_out) continue;
    const float xo = sqrtf(r_out * r_out - dy * dy);
    const int x_start = static_cast<int>(floorf(cx - xo));
    const int x_end = static_cast<int>(ceilf(cx + xo));
    for (int x = x_start < 0 ? 0 : x_start; x <= x_end && x < kSize; ++x) {
      const float dx = static_cast<float>(x) + 0.5f - cx;
      const float r = sqrtf(dx * dx + dy * dy);
      if (r > r_out || r < r_in) continue;
      const float phi = atan2f(dx, dy);
      const int u = static_cast<int>(floorf(r_mid * phi + u0));
      int v = static_cast<int>(r_out - r);
      if (v >= s_band_h) v = s_band_h - 1;
      if (u < 0 || u >= s_text_w || v < 0) continue;
      const uint8_t px = s_buf[v * s_text_w + u];
      if (px == 0) continue;
      tft.drawPixel(x, y, px >= 0xF0 ? ink : edge);
    }
  }
  tft.endWrite();
}

void drawPhoto(size_t i) {
  tft.drawJpg(kSplashPhotos[i].data, kSplashPhotos[i].len, 0, 0);
}

/** Open the photo from the middle outwards over whatever is on screen. */
void wipeInPhoto(size_t i) {
  using namespace photosplash;
  for (int s = 1; s <= kWipeSteps; ++s) {
    const int w = kSize * s / kWipeSteps;
    tft.setClipRect((kSize - w) / 2, 0, w, kSize);
    drawPhoto(i);
    tft.clearClipRect();
    delay(kWipeStepMs);
  }
}

/** Close black curtains in from both sides. */
void closeToBlack() {
  using namespace photosplash;
  const uint16_t black = tft.color565(0, 0, 0);
  for (int s = 1; s <= kWipeSteps; ++s) {
    const int w = (kSize / 2) * s / kWipeSteps;
    tft.fillRect(0, 0, w, kSize, black);
    tft.fillRect(kSize - w, 0, w, kSize, black);
    delay(kWipeStepMs);
  }
}

}  // namespace

void photoSplashShow() {
  using namespace photosplash;
  tft.fillScreen(tft.color565(0, 0, 0));
  s_have_caption = buildCaption();

  for (size_t i = 0; i < kSplashPhotoCount; ++i) {
    wipeInPhoto(i);
    drawCaption();
    delay(i + 1 == kSplashPhotoCount ? kLastHoldMs : kHoldMs);
  }
  closeToBlack();

  s_text.deleteSprite();
  s_buf = nullptr;
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui