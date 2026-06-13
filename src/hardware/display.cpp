#include "hardware/display.h"

#include "config.h"
#include "hardware/display_font.h"

#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
#include <driver/ledc.h>
#endif

LGFX tft;

#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
namespace {
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr uint32_t kBacklightPwmHz = 5000;
bool s_backlight_pwm_ready = false;

void initBacklightPwm() {
  if (config::kDisplayPinBl == GPIO_NUM_NC || s_backlight_pwm_ready) {
    return;
  }
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.timer_num = LEDC_TIMER_0;
  timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
  timer_cfg.freq_hz = kBacklightPwmHz;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_cfg);

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.gpio_num = config::kDisplayPinBl;
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel = kBacklightChannel;
  ch_cfg.timer_sel = LEDC_TIMER_0;
  ch_cfg.intr_type = LEDC_INTR_DISABLE;
  ch_cfg.duty = 0;
  ch_cfg.hpoint = 0;
  ledc_channel_config(&ch_cfg);
  s_backlight_pwm_ready = true;
}

void setBacklightDuty(uint8_t brightness) {
  if (!s_backlight_pwm_ready) {
    return;
  }
  const uint32_t duty = config::kDisplayBacklightInvert
                            ? (255U - brightness)
                            : static_cast<uint32_t>(brightness);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
}
}  // namespace
#endif

static void deselectSharedSpiDevices() {
#if defined(PLANE_RADAR_BOARD_ESP32_TFT_ROUND)
  if (board::kSdCardPinCs != GPIO_NUM_NC &&
      board::kSdCardPinCs != board::kDisplayPinCs) {
    pinMode(static_cast<uint8_t>(board::kSdCardPinCs), OUTPUT);
    digitalWrite(static_cast<uint8_t>(board::kSdCardPinCs), HIGH);
  }
#endif
}

void displayInit() {
  deselectSharedSpiDevices();
#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
  initBacklightPwm();
  setBacklightDuty(0);
#else
  if (config::kDisplayPinBl != GPIO_NUM_NC) {
    pinMode(static_cast<uint8_t>(config::kDisplayPinBl), OUTPUT);
    digitalWrite(static_cast<uint8_t>(config::kDisplayPinBl), LOW);
  }
#endif
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
  tft.fillScreen(config::kColorBlack);
}

void displayBacklightOn() {
#if defined(PLANE_RADAR_BOARD_ESP32_P4_WAVESHARE_4C)
  setBacklightDuty(255);
#else
  if (config::kDisplayPinBl != GPIO_NUM_NC) {
    digitalWrite(static_cast<uint8_t>(config::kDisplayPinBl), HIGH);
  }
#endif
}
