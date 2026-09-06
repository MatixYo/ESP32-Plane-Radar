#include "hardware/display.h"

#include "hardware/display_font.h"
#include "ui/radar_theme.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
  // Panel size comes from the driver rather than config.h, so the layout
  // follows whichever panel is actually fitted.
  ui::radar::initMetrics(tft.width(), tft.height());
}
