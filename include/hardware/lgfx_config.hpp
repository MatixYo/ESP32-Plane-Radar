#pragma once

#define LGFX_USE_V1

#include <LovyanGFX.hpp>

#include "config.h"

/**
 * LovyanGFX device configuration for the ESP32-2424S012:
 *
 *   ESP32-C3
 *   GC9A01 240x240 round LCD
 *   SPI2
 */
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;

public:
  LGFX()
  {
    // --- SPI bus ---
    {
      auto cfg = _bus.config();

      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;

      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.freq_read = config::kDisplaySpiReadHz;

      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk =
          static_cast<int>(config::kDisplayPinSclk);

      cfg.pin_mosi =
          static_cast<int>(config::kDisplayPinMosi);

      cfg.pin_miso = -1;

      cfg.pin_dc =
          static_cast<int>(config::kDisplayPinDc);

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    // --- GC9A01 panel ---
    {
      auto cfg = _panel.config();

      cfg.pin_cs =
          static_cast<int>(config::kDisplayPinCs);

      cfg.pin_rst =
          static_cast<int>(config::kDisplayPinRst);

      cfg.pin_busy = -1;

      cfg.memory_width = config::kDisplayWidth;
      cfg.memory_height = config::kDisplayHeight;

      cfg.panel_width = config::kDisplayWidth;
      cfg.panel_height = config::kDisplayHeight;

      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;

      cfg.readable = false;

      cfg.invert =
          config::kDisplayInvert;

      cfg.rgb_order =
          config::kDisplayRgbOrder;

      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel.config(cfg);
    }

    setPanel(&_panel);
  }
};