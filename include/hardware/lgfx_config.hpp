#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "hardware/display_bus_policy.h"
#ifdef BOARD_NM_TV_154
#include "hardware/nm_tv_154_pins.h"
#endif

/** LovyanGFX device for the selected 240x240 SPI panel. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
#ifdef BOARD_NM_TV_154
  lgfx::Panel_ST7789 _panel;
  lgfx::Light_PWM _light;
#else
  lgfx::Panel_GC9A01 _panel;
#endif

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
#ifdef BOARD_NM_TV_154
      cfg.spi_mode = hardware::display::spiModeForBoard(true);
#else
      cfg.spi_mode = hardware::display::spiModeForBoard(false);
#endif
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
#ifdef BOARD_NM_TV_154
      cfg.panel_width = config::kDisplayWidth;
      cfg.panel_height = config::kDisplayHeight;
      cfg.memory_width = config::kDisplayWidth;
      cfg.memory_height = 320;
      cfg.offset_x = 0;
      // TFT_eSPI's 240x240 CGRAM_OFFSET mapping starts at row 0 in rotation 0.
      cfg.offset_y = 0;
#endif
      _panel.config(cfg);
    }
#ifdef BOARD_NM_TV_154
    {
      auto cfg = _light.config();
      cfg.pin_bl = hardware::nm_tv_154::kLcdBacklightPin;
      cfg.invert = hardware::nm_tv_154::kLcdBacklightActiveLow;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif
    setPanel(&_panel);
  }
};
