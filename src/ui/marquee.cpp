#include "ui/marquee.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <esp_system.h>

#include <cmath>
#include <cstdint>

#include "hardware/display.h"
#include "ui/marquee_config.h"
#include "ui/radar_theme.h"

namespace ui {
namespace {

constexpr float kPi = 3.14159265f;
/** The text runs +/- this far either side of straight up (stops short of E/W). */
constexpr float kHalfSpanRad = 75.0f * kPi / 180.0f;
/** Blank run between the end of the message and its next pass (px). */
constexpr float kGapPx = 40.0f;
/** Longest message strip we will allocate (px wide). */
constexpr int kMaxStripW = 800;

bool s_scheduled = false;
bool s_active = false;
unsigned long s_next_start_ms = 0;
unsigned long s_window_start_ms = 0;
int s_last_message = -1;

LGFX_Sprite s_strip(&tft);  // the message, rendered flat, then bent onto the arc
const uint8_t* s_strip_buf = nullptr;
int s_text_w = 0;
int s_band_h = 0;

/** Render `msg` flat into s_strip (1 byte per pixel, non-zero = ink). */
bool buildStrip(const char* msg) {
  int text_w = 0;
  int band_h = 0;
  {
    LGFX_Sprite probe(&tft);  // tiny sprite, just to measure the font
    probe.setColorDepth(lgfx::color_depth_t::rgb332_1Byte);
    if (!probe.createSprite(1, 1)) {
      return false;
    }
    probe.setFont(&fonts::FreeSansBold9pt7b);
    text_w = probe.textWidth(msg);
    band_h = probe.fontHeight();
    probe.deleteSprite();
  }
  if (text_w < 1 || band_h < 8 || band_h > 40) {
    return false;
  }
  if (text_w > kMaxStripW) {
    text_w = kMaxStripW;
  }

  s_strip.setColorDepth(lgfx::color_depth_t::rgb332_1Byte);
  if (!s_strip.createSprite(text_w, band_h)) {
    return false;
  }
  s_strip.fillSprite(0);
  s_strip.setFont(&fonts::FreeSansBold9pt7b);
  s_strip.setTextDatum(textdatum_t::top_left);
  s_strip.setTextColor(0xFFFFu);
  s_strip.drawString(msg, 0, 0);

  s_strip_buf = static_cast<const uint8_t*>(s_strip.getBuffer());
  s_text_w = text_w;
  s_band_h = band_h;
  return s_strip_buf != nullptr;
}

void stopWindow() {
  s_strip.deleteSprite();
  s_strip_buf = nullptr;
  s_active = false;
}

void startWindow(unsigned long start_ms) {
  const size_t n = marquee::kMessageCount;
  if (n == 0) {
    return;
  }
  size_t idx = 0;
  if (n > 1 && s_last_message >= 0) {
    const size_t r = esp_random() % (n - 1);  // any message except the last one
    idx = r >= static_cast<size_t>(s_last_message) ? r + 1 : r;
  } else {
    idx = esp_random() % n;
  }
  const char* msg = marquee::kMessages[idx];
  if (!buildStrip(msg)) {
    Serial.println("marquee: could not build message strip");
    return;
  }
  s_last_message = static_cast<int>(idx);
  s_window_start_ms = start_ms;
  s_active = true;
  Serial.printf("marquee: \"%s\" for %lu s\n", msg, marquee::kDurationMs / 1000UL);
}

void tick() {
  const unsigned long now = millis();
  if (!s_scheduled) {
    s_next_start_ms = now + marquee::kFirstDelayMs;
    s_scheduled = true;
  }
  if (s_active && now - s_window_start_ms >= marquee::kDurationMs) {
    stopWindow();
  }
  if (s_active) {
    return;
  }
  // Skip windows that were missed completely (e.g. Wi-Fi was down).
  while (static_cast<long>(now - s_next_start_ms) >=
         static_cast<long>(marquee::kDurationMs)) {
    s_next_start_ms += marquee::kIntervalMs;
  }
  if (static_cast<long>(now - s_next_start_ms) >= 0) {
    startWindow(s_next_start_ms);
    s_next_start_ms += marquee::kIntervalMs;
  }
}

}  // namespace

bool marqueeActive() {
  tick();
  return s_active;
}

void marqueeDraw(lgfx::LGFXBase& gfx) {
  tick();
  if (!s_active || s_strip_buf == nullptr) {
    return;
  }

  // Where the message is along the arc (px of arc length at mid radius,
  // 0 = straight up, positive = clockwise). It rolls right to left.
  const float r_out = static_cast<float>(radar::kSize) * 0.5f;
  const float r_in = r_out - static_cast<float>(s_band_h);
  const float r_mid = (r_out + r_in) * 0.5f;
  const float s_max = r_mid * kHalfSpanRad;
  const float travel = 2.0f * s_max + static_cast<float>(s_text_w) + kGapPx;
  const float t = static_cast<float>(millis() - s_window_start_ms) * 0.001f;
  const float s0 = s_max - fmodf(t * marquee::kSpeedPxPerSec, travel);
  if (s0 >= s_max || s0 + static_cast<float>(s_text_w) <= -s_max) {
    return;  // nothing in view right now: leave the radar untouched
  }

  const float cx = static_cast<float>(radar::kCenterX);
  const float cy = static_cast<float>(radar::kCenterY);

  // Inverse mapping: for every screen pixel in the top band, find which flat
  // strip pixel lands there. This leaves no holes, and the letters come out
  // upright with their tops towards the edge.
  for (int y = 0; y < radar::kCenterY; ++y) {
    const float dy = cy - (static_cast<float>(y) + 0.5f);
    if (dy >= r_out) {
      continue;
    }
    const float xo = sqrtf(r_out * r_out - dy * dy);
    const float xi = dy < r_in ? sqrtf(r_in * r_in - dy * dy) : 0.0f;

    for (int side = 0; side < 2; ++side) {
      float xa = side == 0 ? cx - xo : cx + xi;
      float xb = side == 0 ? cx - xi : cx + xo;
      int x_start = static_cast<int>(floorf(xa));
      int x_end = static_cast<int>(ceilf(xb));
      if (x_start < 0) x_start = 0;
      if (x_end > radar::kSize - 1) x_end = radar::kSize - 1;

      for (int x = x_start; x <= x_end; ++x) {
        const float dx = static_cast<float>(x) + 0.5f - cx;
        const float r = sqrtf(dx * dx + dy * dy);
        if (r > r_out || r < r_in) {
          continue;
        }
        const float phi = atan2f(dx, dy);  // 0 = up, clockwise positive
        if (fabsf(phi) > kHalfSpanRad) {
          continue;
        }
        const int u = static_cast<int>(floorf(r_mid * phi - s0));
        int v = static_cast<int>(r_out - r);  // 0 at the outer edge
        if (v >= s_band_h) v = s_band_h - 1;

        bool ink = false;
        if (u >= 0 && u < s_text_w && v >= 0) {
          ink = s_strip_buf[v * s_text_w + u] != 0;
        }
        gfx.drawPixel(x, y, ink ? radar::kColorOverhead : radar::kColorBackground);
      }
    }
  }
}

}  // namespace ui