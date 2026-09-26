#include "ui/aircraft_detail.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "ui/radar_display.h"

namespace ui {

namespace {

constexpr int kCenterX = config::kDisplayWidth / 2;
constexpr int kCenterY = config::kDisplayHeight / 2;

// Aviation color palette (Glass Cockpit EFIS style)
constexpr uint16_t kColBg = 0x0000;          // Black
constexpr uint16_t kColCyan = 0x07FF;        // Crisp cyan
constexpr uint16_t kColGreen = 0x07E0;       // Radar green
constexpr uint16_t kColAmber = 0xFDA0;       // Warm cockpit amber
constexpr uint16_t kColYellow = 0xFFE0;      // Bright neon yellow
constexpr uint16_t kColRed = 0xF800;         // Warning red
constexpr uint16_t kColWhite = 0xFFFF;       // Pure white
constexpr uint16_t kColDim = 0x7BEF;         // Slate gray label
constexpr uint16_t kColPanelBg = 0x0862;     // Dark navy/gray panel
constexpr uint16_t kColPanelBorder = 0x1945; // Subtle border

const char* cardinalName(float deg) {
  while (deg < 0.0f) deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  const int val = static_cast<int>((deg + 22.5f) / 45.0f) % 8;
  switch (val) {
    case 0: return "S";
    case 1: return "SV";
    case 2: return "V";
    case 3: return "JV";
    case 4: return "J";
    case 5: return "JZ";
    case 6: return "Z";
    case 7: return "SZ";
    default: return "S";
  }
}

const char* friendlyModelName(const char* type, const char* desc) {
  if (desc != nullptr && desc[0] != '\0' && strlen(desc) > 3) {
    return desc;
  }
  if (strcmp(type, "B738") == 0) return "Boeing 737-800";
  if (strcmp(type, "B737") == 0) return "Boeing 737";
  if (strcmp(type, "B739") == 0) return "Boeing 737-900";
  if (strcmp(type, "B38M") == 0) return "Boeing 737 MAX 8";
  if (strcmp(type, "B39M") == 0) return "Boeing 737 MAX 9";
  if (strcmp(type, "A320") == 0) return "Airbus A320";
  if (strcmp(type, "A20N") == 0) return "Airbus A320neo";
  if (strcmp(type, "A321") == 0) return "Airbus A321";
  if (strcmp(type, "A21N") == 0) return "Airbus A321neo";
  if (strcmp(type, "A319") == 0) return "Airbus A319";
  if (strcmp(type, "A332") == 0) return "Airbus A330-200";
  if (strcmp(type, "A333") == 0) return "Airbus A330-300";
  if (strcmp(type, "A359") == 0) return "Airbus A350-900";
  if (strcmp(type, "B77W") == 0) return "Boeing 777-300ER";
  if (strcmp(type, "B789") == 0) return "Boeing 787-9";
  if (strcmp(type, "B788") == 0) return "Boeing 787-8";
  if (strcmp(type, "AT76") == 0) return "ATR 72-600";
  if (strcmp(type, "DH8D") == 0) return "Dash 8 Q400";
  if (strcmp(type, "C172") == 0) return "Cessna 172 Skyhawk";
  if (strcmp(type, "C152") == 0) return "Cessna 152";
  if (strcmp(type, "E190") == 0) return "Embraer E190";
  if (strcmp(type, "E195") == 0) return "Embraer E195";
  if (strcmp(type, "BCS3") == 0) return "Airbus A220-300";
  if (strcmp(type, "GLID") == 0) return "Větroň (Glider)";
  if (strcmp(type, "EC35") == 0) return "Eurocopter EC135";
  if (strcmp(type, "H145") == 0) return "Airbus H145";
  return type[0] != '\0' ? type : "Neznámý typ";
}

}  // namespace

void aircraftDetailInit() {
  // Uses sharedFrameSprite() from radar_display
}

void aircraftDetailDraw(const services::adsb::Aircraft& ac) {
  if (!ensureSharedFrameSprite()) {
    return;
  }
  lgfx::LGFX_Sprite& sprite = sharedFrameSprite();

  sprite.fillScreen(kColBg);

  // Outer bezel ring with cardinal ticks
  sprite.drawCircle(kCenterX, kCenterY, 175, kColPanelBorder);
  sprite.drawFastVLine(kCenterX, 5, 8, kColGreen);
  sprite.drawFastVLine(kCenterX, 347, 8, kColGreen);
  sprite.drawFastHLine(5, kCenterY, 8, kColGreen);
  sprite.drawFastHLine(347, kCenterY, 8, kColGreen);

  // Top header badge
  sprite.setTextDatum(textdatum_t::top_center);
  sprite.setTextColor(kColCyan, kColBg);
  sprite.setFont(&fonts::Font0);
  sprite.drawString("● AIRCRAFT TELEMETRY ●", kCenterX, 22);

  // Callsign (Large Bold)
  sprite.setTextColor(kColYellow, kColBg);
  sprite.setFont(&fonts::FreeSansBold12pt7b);
  char cs[16];
  if (ac.callsign[0] != '\0') {
    snprintf(cs, sizeof(cs), "%s", ac.callsign);
  } else {
    snprintf(cs, sizeof(cs), "HEX:%s", ac.hex[0] != '\0' ? ac.hex : "???");
  }
  sprite.drawString(cs, kCenterX, 42);

  // Registration & Hex transponder code
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColAmber, kColBg);
  char sub_ident[48];
  snprintf(sub_ident, sizeof(sub_ident), "REG: %s  |  ICAO: %s",
           ac.reg[0] != '\0' ? ac.reg : "--",
           ac.hex[0] != '\0' ? ac.hex : "--");
  sprite.drawString(sub_ident, kCenterX, 72);

  // Divider line
  sprite.drawFastHLine(40, 88, 280, kColPanelBorder);
  sprite.drawFastHLine(120, 88, 120, kColCyan);

  // Grid Data Cards
  // Column 1: X = 48 to 170 | Column 2: X = 190 to 312
  constexpr int kCol1X = 50;
  constexpr int kCol2X = 188;
  constexpr int kBoxW = 122;
  constexpr int kBoxH = 48;

  // Box 1: TYPE & MODEL (Row 1, Col 1)
  constexpr int kRow1Y = 96;
  sprite.fillRoundRect(kCol1X, kRow1Y, kBoxW, kBoxH, 4, kColPanelBg);
  sprite.drawRoundRect(kCol1X, kRow1Y, kBoxW, kBoxH, 4, kColPanelBorder);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("TYPE / MODEL", kCol1X + 6, kRow1Y + 4);
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  sprite.setTextColor(kColWhite, kColPanelBg);
  sprite.drawString(ac.type[0] != '\0' ? ac.type : "--", kCol1X + 6, kRow1Y + 16);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColCyan, kColPanelBg);
  char model_buf[20];
  snprintf(model_buf, sizeof(model_buf), "%.16s", friendlyModelName(ac.type, ac.desc));
  sprite.drawString(model_buf, kCol1X + 6, kRow1Y + 34);

  // Box 2: SQUAWK (Row 1, Col 2)
  sprite.fillRoundRect(kCol2X, kRow1Y, kBoxW, kBoxH, 4, kColPanelBg);
  sprite.drawRoundRect(kCol2X, kRow1Y, kBoxW, kBoxH, 4, kColPanelBorder);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("SQUAWK", kCol2X + 6, kRow1Y + 4);
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  const bool is_emergency = (strcmp(ac.squawk, "7700") == 0 || strcmp(ac.squawk, "7600") == 0);
  sprite.setTextColor(is_emergency ? kColRed : kColAmber, kColPanelBg);
  sprite.drawString(ac.squawk[0] != '\0' ? ac.squawk : "----", kCol2X + 6, kRow1Y + 16);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(is_emergency ? kColRed : kColDim, kColPanelBg);
  sprite.drawString(is_emergency ? "! EMERGENCY !" : "Transponder", kCol2X + 6, kRow1Y + 34);

  // Box 3: ALTITUDE (Row 2, Col 1)
  constexpr int kRow2Y = 152;
  sprite.fillRoundRect(kCol1X, kRow2Y, kBoxW, kBoxH, 4, kColPanelBg);
  sprite.drawRoundRect(kCol1X, kRow2Y, kBoxW, kBoxH, 4, kColPanelBorder);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("ALTITUDE", kCol1X + 6, kRow2Y + 4);
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  sprite.setTextColor(kColGreen, kColPanelBg);
  sprite.drawString(ac.alt[0] != '\0' ? ac.alt : "GND", kCol1X + 6, kRow2Y + 16);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  if (ac.on_ground) {
    sprite.drawString("Na zemi (GND)", kCol1X + 6, kRow2Y + 34);
  } else {
    // Parse feet to meters
    int feet = 0;
    sscanf(ac.alt, "%d", &feet);
    char m_buf[20];
    snprintf(m_buf, sizeof(m_buf), "~%d m", static_cast<int>(feet * 0.3048f));
    sprite.drawString(m_buf, kCol1X + 6, kRow2Y + 34);
  }

  // Box 4: VERTICAL SPEED (Row 2, Col 2)
  sprite.fillRoundRect(kCol2X, kRow2Y, kBoxW, kBoxH, 4, kColPanelBg);
  sprite.drawRoundRect(kCol2X, kRow2Y, kBoxW, kBoxH, 4, kColPanelBorder);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("V/S (STOUPÁNÍ)", kCol2X + 6, kRow2Y + 4);
  char vs_buf[20];
  const float vs = ac.baro_rate_fpm;
  uint16_t vs_color = kColDim;
  const char* vs_sub = "LEVEL (rovný)";
  if (vs > 100.0f) {
    snprintf(vs_buf, sizeof(vs_buf), "+%d fpm", static_cast<int>(vs));
    vs_color = kColGreen;
    vs_sub = "⬆ Stoupá";
  } else if (vs < -100.0f) {
    snprintf(vs_buf, sizeof(vs_buf), "%d fpm", static_cast<int>(vs));
    vs_color = kColAmber;
    vs_sub = "⬇ Klesá";
  } else {
    snprintf(vs_buf, sizeof(vs_buf), "0 fpm");
    vs_color = kColWhite;
    vs_sub = "Rovný let";
  }
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  sprite.setTextColor(vs_color, kColPanelBg);
  sprite.drawString(vs_buf, kCol2X + 6, kRow2Y + 16);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(vs_color, kColPanelBg);
  sprite.drawString(vs_sub, kCol2X + 6, kRow2Y + 34);

  // Box 5: SPEED (Row 3, Col 1)
  constexpr int kRow3Y = 208;
  sprite.fillRoundRect(kCol1X, kRow3Y, kBoxW, kBoxH, 4, kColPanelBg);
  sprite.drawRoundRect(kCol1X, kRow3Y, kBoxW, kBoxH, 4, kColPanelBorder);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("GROUND SPEED", kCol1X + 6, kRow3Y + 4);
  char gs_buf[20];
  snprintf(gs_buf, sizeof(gs_buf), "%d kt", static_cast<int>(ac.gs_knots));
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  sprite.setTextColor(kColWhite, kColPanelBg);
  sprite.drawString(gs_buf, kCol1X + 6, kRow3Y + 16);
  char kmh_buf[20];
  snprintf(kmh_buf, sizeof(kmh_buf), "~%d km/h", static_cast<int>(ac.gs_knots * 1.852f));
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString(kmh_buf, kCol1X + 6, kRow3Y + 34);

  // Box 6: HEADING & TRACK (Row 3, Col 2)
  sprite.fillRoundRect(kCol2X, kRow3Y, kBoxW, kBoxH, 4, kColPanelBg);
  sprite.drawRoundRect(kCol2X, kRow3Y, kBoxW, kBoxH, 4, kColPanelBorder);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("KURZ / SMĚR", kCol2X + 6, kRow3Y + 4);
  char hdg_buf[20];
  snprintf(hdg_buf, sizeof(hdg_buf), "%d° (%s)", static_cast<int>(ac.track_deg),
           cardinalName(ac.track_deg));
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  sprite.setTextColor(kColWhite, kColPanelBg);
  sprite.drawString(hdg_buf, kCol2X + 6, kRow3Y + 16);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("Track over ground", kCol2X + 6, kRow3Y + 34);

  // Full-width Radar Vector Panel (Row 4)
  constexpr int kRow4Y = 272;
  constexpr int kVecW = 264;
  constexpr int kVecH = 44;
  constexpr int kVecX = (config::kDisplayWidth - kVecW) / 2;
  sprite.fillRoundRect(kVecX, kRow4Y, kVecW, kVecH, 5, kColPanelBg);
  sprite.drawRoundRect(kVecX, kRow4Y, kVecW, kVecH, 5, kColCyan);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColDim, kColPanelBg);
  sprite.drawString("POLOHA VŮČI VAŠEMU RADARU", kCenterX, kRow4Y + 12);
  char vec_str[64];
  snprintf(vec_str, sizeof(vec_str), "VZDÁLENOST: %.1f km  |  AZIMUT: %d° (%s)",
           ac.dist_km, static_cast<int>(ac.bearing_deg), cardinalName(ac.bearing_deg));
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(kColYellow, kColPanelBg);
  sprite.drawString(vec_str, kCenterX, kRow4Y + 30);

  sprite.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui
