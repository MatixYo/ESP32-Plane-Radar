#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"
#include "hardware/panel_jd9365_waveshare_4c.hpp"

/** LovyanGFX device: JD9365 on MIPI DSI (Waveshare ESP32-P4 4C). */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_DSI _bus;
  lgfx::Panel_JD9365_Waveshare4C _panel;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.lane_mbps = board::kMipiLaneMbps;
      cfg.lane_num = board::kMipiLaneNum;
      cfg.ldo_chan_id = board::kMipiLdoChan;
      cfg.ldo_voltage_mv = board::kMipiLdoVoltageMv;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.panel_width = board::kDisplayWidth;
      cfg.panel_height = board::kDisplayHeight;
      cfg.memory_width = board::kDisplayWidth;
      cfg.memory_height = board::kDisplayHeight;
      cfg.pin_rst = static_cast<int>(board::kDisplayPinRst);
      cfg.invert = board::kDisplayInvert;
      cfg.rgb_order = board::kDisplayRgbOrder;
      _panel.config(cfg);
    }
    {
      auto detail = _panel.config_detail();
      detail.dpi_freq_mhz = board::kDpiClockMhz;
      detail.hsync_pulse_width = board::kDpiHsyncPulse;
      detail.hsync_back_porch = board::kDpiHsyncBackPorch;
      detail.hsync_front_porch = board::kDpiHsyncFrontPorch;
      detail.vsync_pulse_width = board::kDpiVsyncPulse;
      detail.vsync_back_porch = board::kDpiVsyncBackPorch;
      detail.vsync_front_porch = board::kDpiVsyncFrontPorch;
      _panel.config_detail(detail);
    }
    setPanel(&_panel);
  }
};
