#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/** LovyanGFX device: GC9A01 on SPI. Pin values come from config.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;
#ifdef DISPLAY_HAS_BACKLIGHT
  lgfx::Light_PWM _light;
#endif

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = -1;
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      _panel.config(cfg);
    }
#ifdef DISPLAY_HAS_BACKLIGHT
    {
      auto cfg = _light.config();
      cfg.pin_bl = static_cast<int>(config::kDisplayPinBl);
      cfg.invert = false;  // active HIGH
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);  // enables tft.setBrightness()
    }
#endif
    setPanel(&_panel);
  }
};
