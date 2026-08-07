/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include <time.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/time_settings.h"
#include "services/wifi_setup.h"
#include "ui/radar_animation_policy.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"
#ifdef BOARD_NM_TV_154
#include "ui/nm_tv_154_policy.h"
#endif

namespace {

volatile bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_animation_refresh_ms = 0;
#ifdef BOARD_NM_TV_154
constexpr uint16_t kTouchPressedThreshold = 90;
constexpr unsigned long kSquareStatusRefreshIntervalMs = 1000UL;
portMUX_TYPE g_adsb_update_lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_adsb_fetch_task = nullptr;
volatile bool g_adsb_refresh_pending = false;
volatile unsigned long g_background_adsb_update_ms = 0;
unsigned long g_last_background_adsb_fetch_ms = 0;
unsigned long g_last_square_status_refresh_ms = 0;
bool g_clock_started = false;
ui::nm_tv_154::TouchRangeState g_touch_range_state;
#endif

#ifdef BOARD_NM_TV_154
constexpr bool kIsNmTv154 = true;
#else
constexpr bool kIsNmTv154 = false;
#endif

void startLocalClock() {
#ifdef BOARD_NM_TV_154
  if (g_clock_started || WiFi.status() != WL_CONNECTED) {
    return;
  }
  configTime(0, 0, config::kNtpServer);
  g_clock_started = true;
  Serial.printf("Clock sync: %s\n", config::kNtpServer);
#endif
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  startLocalClock();
  const unsigned long draw_started_ms = millis();
  ui::radarDisplayDraw();
  g_last_animation_refresh_ms = draw_started_ms;
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    const unsigned long draw_started_ms = millis();
    ui::radarDisplayDraw();
    g_last_animation_refresh_ms = draw_started_ms;
  }
}

void handleNmTv154RangeTouch() {
#ifdef BOARD_NM_TV_154
  const uint16_t raw = touchRead(T9);
  const bool is_down = raw < kTouchPressedThreshold;
  const bool was_down = g_touch_range_state.was_down;
  g_touch_range_state = ui::nm_tv_154::nextTouchRangeState(g_touch_range_state,
                                                            is_down);
  if (is_down && !was_down) {
    Serial.printf("[touch] pressed, raw=%u\n", static_cast<unsigned>(raw));
  }
  if (g_touch_range_state.range_tap) {
    Serial.println("[touch] tap -> range");
    onRangeTap();
  }
#endif
}

void serviceRadarAnimation() {
  if (!g_radar_visible || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long now_ms = millis();
  if (!ui::radar::animationNeeded(ui::radar::showSweep(),
                                  services::adsb::aircraftCount()) ||
      now_ms - g_last_animation_refresh_ms <
          ui::radar::animationIntervalMs(kIsNmTv154)) {
    return;
  }

  g_last_animation_refresh_ms = now_ms;
  ui::radarDisplayRefreshAircraft();
}

void pollNetwork() {
  wifiLoop();
  serviceRadarAnimation();
  handleNmTv154RangeTouch();
}

#ifdef BOARD_NM_TV_154
void pollBackgroundNetwork() { taskYIELD(); }

void fetchAndRequestAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    return;
  }

  taskENTER_CRITICAL(&g_adsb_update_lock);
  g_background_adsb_update_ms = millis();
  g_adsb_refresh_pending = true;
  taskEXIT_CRITICAL(&g_adsb_update_lock);
}

bool consumeBackgroundAdsbUpdate(unsigned long* update_ms) {
  taskENTER_CRITICAL(&g_adsb_update_lock);
  const bool pending = g_adsb_refresh_pending;
  if (pending) {
    *update_ms = g_background_adsb_update_ms;
    g_adsb_refresh_pending = false;
  }
  taskEXIT_CRITICAL(&g_adsb_update_lock);
  return pending;
}

void adsbFetchTask(void*) {
  for (;;) {
    if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
      const unsigned long now_ms = millis();
      if (now_ms - g_last_background_adsb_fetch_ms >=
          config::kAdsbFetchIntervalMs) {
        g_last_background_adsb_fetch_ms = now_ms;
        fetchAndRequestAircraft();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
#endif

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
  handleNmTv154RangeTouch();
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
#ifdef BOARD_NM_TV_154
    ui::radarDisplayRefreshAircraft();
#endif
    handleBootButton();
    return;
  }
  const unsigned long draw_started_ms = millis();
  ui::radarDisplayMarkDataUpdated(millis());
  ui::radarDisplayRefreshAircraft();
  g_last_animation_refresh_ms = draw_started_ms;
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.printf("Plane Radar %s\n", config::kFirmwareVersion);

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  services::time_settings::init();
  ui::radar::rangeInit();
#ifdef BOARD_NM_TV_154
  services::adsb::setPollFn(pollBackgroundNetwork);
#else
  services::adsb::setPollFn(pollNetwork);
#endif
#ifdef BOARD_NM_TV_154
  Serial.printf("[touch] ready, raw=%u\n",
                static_cast<unsigned>(touchRead(T9)));
#endif

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
#ifdef BOARD_NM_TV_154
  if (xTaskCreatePinnedToCore(adsbFetchTask, "adsb-fetch", 8192, nullptr, 1,
                              &g_adsb_fetch_task, 0) != pdPASS) {
    Serial.println("adsb: background task create failed");
  }
#endif
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
#ifdef BOARD_NM_TV_154
      ui::radarDisplayRefreshAircraft();
#endif
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else {
#ifdef BOARD_NM_TV_154
      unsigned long update_ms = 0;
      if (consumeBackgroundAdsbUpdate(&update_ms)) {
        const unsigned long draw_started_ms = millis();
        ui::radarDisplayMarkDataUpdated(update_ms);
        ui::radarDisplayRefreshAircraft();
        g_last_animation_refresh_ms = draw_started_ms;
      } else {
        serviceRadarAnimation();
        const unsigned long status_now_ms = millis();
        if (status_now_ms - g_last_square_status_refresh_ms >=
            kSquareStatusRefreshIntervalMs) {
          g_last_square_status_refresh_ms = status_now_ms;
          ui::radarDisplayRefreshStatus();
        }
      }
#else
      const unsigned long now_ms = millis();
      if (now_ms - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
        g_last_adsb_fetch_ms = now_ms;
        fetchAndDrawAircraft();
      } else {
        serviceRadarAnimation();
      }
#endif
    }
  }

  delay(10);
}
