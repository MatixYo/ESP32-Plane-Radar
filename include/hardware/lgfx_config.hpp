#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "hardware/board.h"

/** LovyanGFX device: GC9A01 on SPI. Pin values come from hardware/board.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;

public:
  LGFX() {
    applyBoard(hardware::board::pins(hardware::board::compileDefault()));
    setPanel(&_panel);
  }

  /** (Re)configure SPI bus and panel for a board. Call before init(). */
  void applyBoard(const hardware::board::DisplayPins& p) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = p.pin_sclk;
      cfg.pin_mosi = p.pin_mosi;
      cfg.pin_miso = -1;
      cfg.pin_dc = p.pin_dc;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = p.pin_cs;
      cfg.pin_rst = p.pin_rst;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = p.rgb_order;
      _panel.config(cfg);
    }
  }
};
