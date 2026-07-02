#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "hardware/board.h"

/** LovyanGFX device: GC9A01 on SPI. Pin values come from hardware/board.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;
  lgfx::Touch_CST816S _touch;

public:
  LGFX() {
    applyBoard(hardware::board::pins(hardware::board::compileDefault()));
    setPanel(&_panel);
  }

  /** (Re)configure SPI bus, panel and touch for a board. Call before init(). */
  void applyBoard(const hardware::board::DisplayPins& p) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = p.pin_sclk;
      cfg.pin_mosi = p.pin_mosi;
      cfg.pin_miso = -1;
      cfg.pin_dc = p.pin_dc;
      cfg.dma_channel = SPI_DMA_CH_AUTO;  // non-blocking frame pushes
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
    if (p.touch_sda >= 0) {
      auto cfg = _touch.config();
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x15;  // CST816 default address
      cfg.pin_sda = p.touch_sda;
      cfg.pin_scl = p.touch_scl;
      cfg.pin_int = p.touch_int;
      cfg.pin_rst = p.touch_rst;
      cfg.freq = 400000;
      cfg.x_min = 0;
      cfg.x_max = config::kDisplayWidth - 1;
      cfg.y_min = 0;
      cfg.y_max = config::kDisplayHeight - 1;
      cfg.offset_rotation = 0;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
  }
};
