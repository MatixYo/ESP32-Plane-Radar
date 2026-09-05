#include "ui/clock_display.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "config.h"
#include "hardware/display.h"
#include "services/time_service.h"
#include "ui/radar_display.h"

namespace ui {

namespace {

constexpr int kCenterX = config::kDisplayWidth / 2;
constexpr int kCenterY = config::kDisplayHeight / 2;
constexpr float kDegToRad = 0.0174532925f;

// Clock themes
struct ClockTheme {
  const char* name;
  uint16_t bg_color;
  uint16_t dial_ring_color;
  uint16_t minute_tick_color;
  uint16_t hour_tick_color;
  uint16_t numeral_color;
  uint16_t hour_hand_color;
  uint16_t minute_hand_color;
  uint16_t second_hand_color;
  uint16_t date_bg_color;
  uint16_t date_text_color;
  uint16_t digital_color;
};

const ClockTheme kThemes[] = {
    // 0: Classic Aviation (Crisp White & Radar Orange)
    {"Aviation", 0x0000, 0x2104, 0x5AEB, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFD00, 0x18C3, 0xFFFF, 0xFD00},
    // 1: Radar Emerald (Night Vision)
    {"Radar NVG", 0x0000, 0x0200, 0x03E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0xFFE0, 0x0200, 0x07E0, 0x07E0},
    // 2: Cockpit Amber
    {"Cockpit Amber", 0x0000, 0x2940, 0x8400, 0xFDA0, 0xFDA0, 0xFDA0, 0xFDA0, 0xF800, 0x2940, 0xFDA0, 0xFDA0},
    // 3: Tactical Cyan
    {"Tactical Cyan", 0x0000, 0x0188, 0x2314, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0xFD00, 0x098A, 0x7FFF, 0x7FFF},
};

constexpr size_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
uint8_t s_theme_idx = 0;

const char* kCzechDays[] = {"NE", "PO", "UT", "ST", "CT", "PA", "SO"};
const char* kCzechMonths[] = {"LED", "UNO", "BRE", "DUB", "KVE", "CER",
                              "CRC", "SRP", "ZAR", "RIJ", "LIS", "PRO"};

void drawHand(lgfx::LGFX_Sprite& sprite, float angle_deg, float length, float tail_len,
              float width, uint16_t color) {
  const float rad = (angle_deg - 90.0f) * kDegToRad;
  const float cos_a = cosf(rad);
  const float sin_a = sinf(rad);

  const float norm_rad = angle_deg * kDegToRad;
  const float cos_n = cosf(norm_rad);
  const float sin_n = sinf(norm_rad);

  const float half_w = width * 0.5f;

  const float tip_x = kCenterX + length * cos_a;
  const float tip_y = kCenterY + length * sin_a;

  const float tail_x = kCenterX - tail_len * cos_a;
  const float tail_y = kCenterY - tail_len * sin_a;

  const float side1_x = kCenterX + half_w * cos_n - (tail_len * 0.4f) * cos_a;
  const float side1_y = kCenterY + half_w * sin_n - (tail_len * 0.4f) * sin_a;

  const float side2_x = kCenterX - half_w * cos_n - (tail_len * 0.4f) * cos_a;
  const float side2_y = kCenterY - half_w * sin_n - (tail_len * 0.4f) * sin_a;

  // Draw tapered sword hand
  sprite.fillTriangle(tip_x, tip_y, side1_x, side1_y, side2_x, side2_y, color);
  sprite.fillTriangle(tail_x, tail_y, side1_x, side1_y, side2_x, side2_y, color);
}

void drawSecondHand(lgfx::LGFX_Sprite& sprite, float angle_deg, uint16_t color) {
  const float rad = (angle_deg - 90.0f) * kDegToRad;
  const float cos_a = cosf(rad);
  const float sin_a = sinf(rad);

  const float tip_len = 156.0f;
  const float tail_len = 32.0f;

  const float tip_x = kCenterX + tip_len * cos_a;
  const float tip_y = kCenterY + tip_len * sin_a;

  const float tail_x = kCenterX - tail_len * cos_a;
  const float tail_y = kCenterY - tail_len * sin_a;

  // Main thin needle line
  sprite.drawWideLine(tail_x, tail_y, tip_x, tip_y, 1.5f, color);

  // Counterweight disc near the tail
  const float cw_x = kCenterX - 20.0f * cos_a;
  const float cw_y = kCenterY - 20.0f * sin_a;
  sprite.fillCircle(cw_x, cw_y, 4, color);
  sprite.drawCircle(cw_x, cw_y, 4, 0x0000);
}

void renderClockFace(lgfx::LGFX_Sprite& sprite, const struct tm& tm) {
  const auto& th = kThemes[s_theme_idx];

  sprite.fillScreen(th.bg_color);

  // Bezel ring
  sprite.drawCircle(kCenterX, kCenterY, 178, th.dial_ring_color);
  sprite.drawCircle(kCenterX, kCenterY, 177, th.dial_ring_color);
  sprite.drawCircle(kCenterX, kCenterY, 168, th.dial_ring_color);
  sprite.drawCircle(kCenterX, kCenterY, 115, th.dial_ring_color);

  // 60 minute / second ticks
  for (int i = 0; i < 60; ++i) {
    const float rad = (i * 6.0f - 90.0f) * kDegToRad;
    const float cos_a = cosf(rad);
    const float sin_a = sinf(rad);

    if (i % 5 == 0) {
      // Hour marks (12 bold ticks)
      const float r_inner = (i % 15 == 0) ? 148.0f : 154.0f;
      const float r_outer = 175.0f;
      const float x1 = kCenterX + r_inner * cos_a;
      const float y1 = kCenterY + r_inner * sin_a;
      const float x2 = kCenterX + r_outer * cos_a;
      const float y2 = kCenterY + r_outer * sin_a;
      sprite.drawWideLine(x1, y1, x2, y2, (i % 15 == 0) ? 3.5f : 2.5f, th.hour_tick_color);
    } else {
      // Minute marks (48 fine ticks)
      const float r_inner = 166.0f;
      const float r_outer = 175.0f;
      const float x1 = kCenterX + r_inner * cos_a;
      const float y1 = kCenterY + r_inner * sin_a;
      const float x2 = kCenterX + r_outer * cos_a;
      const float y2 = kCenterY + r_outer * sin_a;
      sprite.drawWideLine(x1, y1, x2, y2, 1.2f, th.minute_tick_color);
    }
  }

  // Hour numerals (12, 1 .. 11)
  sprite.setFont(&fonts::FreeSansBold12pt7b);
  sprite.setTextColor(th.numeral_color);
  sprite.setTextDatum(textdatum_t::middle_center);

  const char* numerals[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"};
  for (int i = 0; i < 12; ++i) {
    // Skip 3 if we place date complication there, or draw all
    if (i == 3) continue; // Leave room for date window

    const float rad = (i * 30.0f - 90.0f) * kDegToRad;
    const float r_num = 134.0f;
    const int x = static_cast<int>(kCenterX + r_num * cosf(rad));
    const int y = static_cast<int>(kCenterY + r_num * sinf(rad));
    sprite.drawString(numerals[i], x, y);
  }

  // Date Complication at 3 o'clock
  {
    const int box_w = 68;
    const int box_h = 28;
    const int box_x = kCenterX + 70;
    const int box_y = kCenterY - box_h / 2;

    sprite.fillRoundRect(box_x, box_y, box_w, box_h, 4, th.date_bg_color);
    sprite.drawRoundRect(box_x, box_y, box_w, box_h, 4, th.hour_tick_color);

    char date_buf[16];
    const int wday = (tm.tm_wday >= 0 && tm.tm_wday < 7) ? tm.tm_wday : 0;
    snprintf(date_buf, sizeof(date_buf), "%s %d", kCzechDays[wday], tm.tm_mday);

    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.setTextColor(th.date_text_color);
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.drawString(date_buf, box_x + box_w / 2, kCenterY);
  }

  // Sub-dial insignia under 12 o'clock
  sprite.setFont(&fonts::Font0);
  sprite.setTextSize(1);
  sprite.setTextColor(th.minute_tick_color);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.drawString("PLANE RADAR", kCenterX, kCenterY - 62);
  sprite.drawString("CHRONOMETER", kCenterX, kCenterY - 50);

  // Digital time readout at 6 o'clock
  char dig_buf[16];
  snprintf(dig_buf, sizeof(dig_buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  sprite.setTextColor(th.digital_color);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.drawString(dig_buf, kCenterX, kCenterY + 65);

  // Calculate hand angles
  const float hour_deg = (tm.tm_hour % 12 + tm.tm_min / 60.0f + tm.tm_sec / 3600.0f) * 30.0f;
  const float min_deg  = (tm.tm_min + tm.tm_sec / 60.0f) * 6.0f;
  const float sec_deg  = tm.tm_sec * 6.0f;

  // Draw Hour Hand
  drawHand(sprite, hour_deg, 92.0f, 20.0f, 8.0f, th.hour_hand_color);
  // Center core outline on hour hand
  drawHand(sprite, hour_deg, 84.0f, 16.0f, 4.0f, th.bg_color);

  // Draw Minute Hand
  drawHand(sprite, min_deg, 138.0f, 24.0f, 6.0f, th.minute_hand_color);
  drawHand(sprite, min_deg, 130.0f, 20.0f, 3.0f, th.bg_color);

  // Draw Second Hand
  drawSecondHand(sprite, sec_deg, th.second_hand_color);

  // Center Pivot Cap
  sprite.fillCircle(kCenterX, kCenterY, 8, th.bg_color);
  sprite.drawCircle(kCenterX, kCenterY, 8, th.hour_tick_color);
  sprite.fillCircle(kCenterX, kCenterY, 5, th.second_hand_color);
  sprite.fillCircle(kCenterX, kCenterY, 2, th.bg_color);
}

}  // namespace

void clockDisplayInit() {
  services::time::init();
}

void clockDisplayDraw(bool /*full_redraw*/) {
  struct tm tm_info;
  const bool has_time = services::time::getLocalTimeInfo(&tm_info);

  if (!has_time) {
    // Default preview time (10:10:30) while waiting for NTP
    tm_info.tm_hour = 10;
    tm_info.tm_min = 10;
    tm_info.tm_sec = 30;
    tm_info.tm_wday = 5;
    tm_info.tm_mday = 4;
    tm_info.tm_mon = 8;
  }

  if (ensureSharedFrameSprite()) {
    lgfx::LGFX_Sprite& frame = sharedFrameSprite();
    renderClockFace(frame, tm_info);

    if (!has_time) {
      frame.setFont(&fonts::FreeSansBold9pt7b);
      frame.setTextColor(0xFFE0);
      frame.setTextDatum(textdatum_t::middle_center);
      frame.drawString("SYNCHRONIZING...", kCenterX, kCenterY - 25);
    }

    frame.pushSprite(0, 0);
  }
}

void clockDisplayNextTheme() {
  s_theme_idx = static_cast<uint8_t>((s_theme_idx + 1) % kThemeCount);
  clockDisplayDraw(true);
}

void clockDisplayPrevTheme() {
  if (s_theme_idx > 0) {
    s_theme_idx--;
  } else {
    s_theme_idx = static_cast<uint8_t>(kThemeCount - 1);
  }
  clockDisplayDraw(true);
}

}  // namespace ui
