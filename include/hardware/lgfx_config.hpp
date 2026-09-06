#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/**
 * LovyanGFX device. Pin values and panel geometry come from config.h.
 *
 * Both panels derive from LovyanGFX's shared Panel_GC9xxx base, so only the
 * type differs — the bus, pins and backlight setup below are identical.
 * Panel_GC9B72 needs LovyanGFX >= 1.2.28.
 */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
#if defined(PLANE_RADAR_PANEL_GC9B72)
  lgfx::Panel_GC9B72 _panel;
#else
  lgfx::Panel_GC9A01 _panel;
#endif
  lgfx::Light_PWM _light;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = config::kDisplayPinMiso;
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
    // Only attach a light when a backlight pin is actually wired; on the GC9A01
    // modules the backlight is tied on at the module and setBrightness is a
    // no-op, which is the behaviour this project has always had.
    if (config::kDisplayPinBacklight >= 0) {
      auto cfg = _light.config();
      cfg.pin_bl = config::kDisplayPinBacklight;
      cfg.invert = false;
      cfg.freq = 44100;
      // The ESP32-C3 has only 6 LEDC channels (0-5), so the 7 used in most
      // LovyanGFX examples (written for the 16-channel ESP32) is out of range.
      cfg.pwm_channel = 0;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
