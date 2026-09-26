/**
 * Plane Radar — WiFi setup, radar UI, target telemetry and analog clock on 360x360 round display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "hardware/encoder.h"
#include "services/adsb_client.h"
#include "services/radar_location.h"
#include "services/time_service.h"
#include "services/wifi_setup.h"
#include "ui/aircraft_detail.h"
#include "ui/clock_display.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

enum class AppMode {
  RADAR,
  CLOCK,
  DETAIL
};

enum class RadarControlMode {
  ZOOM,
  SELECT
};

AppMode g_app_mode = AppMode::RADAR;
RadarControlMode g_radar_ctrl = RadarControlMode::ZOOM;
int g_selected_aircraft_idx = -1;

bool g_screen_ready = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_select_action_ms = 0;
time_t g_last_clock_sec = 0;

void showCurrentScreen() {
  if (WiFi.status() != WL_CONNECTED) {
    g_screen_ready = false;
    return;
  }

  if (g_app_mode == AppMode::RADAR) {
    ui::radarSetSelection(g_radar_ctrl == RadarControlMode::SELECT,
                          g_selected_aircraft_idx);
    ui::radarDisplayDraw();
  } else if (g_app_mode == AppMode::CLOCK) {
    ui::clockDisplayDraw(true);
  } else if (g_app_mode == AppMode::DETAIL) {
    const size_t n = services::adsb::aircraftCount();
    if (n > 0 && g_selected_aircraft_idx >= 0 &&
        g_selected_aircraft_idx < static_cast<int>(n)) {
      ui::aircraftDetailDraw(
          services::adsb::aircraftList()[g_selected_aircraft_idx]);
    } else {
      g_app_mode = AppMode::RADAR;
      g_radar_ctrl = RadarControlMode::ZOOM;
      ui::radarSetSelection(false, -1);
      ui::radarDisplayDraw();
    }
  }
  g_screen_ready = true;
}

void onRangeStep(bool next) {
  if (next) {
    ui::radar::rangeNext();
  } else {
    ui::radar::rangePrev();
  }
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_screen_ready && g_app_mode == AppMode::RADAR &&
      WiFi.status() == WL_CONNECTED) {
    showCurrentScreen();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    if (g_app_mode == AppMode::RADAR &&
        g_radar_ctrl == RadarControlMode::SELECT) {
      g_radar_ctrl = RadarControlMode::ZOOM;
      g_selected_aircraft_idx = -1;
      showCurrentScreen();
    } else {
      onRangeStep(true);
    }
  }
}

void handleEncoder() {
  using hardware::encoder::ButtonAction;
  using hardware::encoder::Rotation;

  const Rotation rot = hardware::encoder::pollRotation();
  if (rot != Rotation::NONE) {
    const bool is_cw = (rot == Rotation::CW);
    const size_t n = services::adsb::aircraftCount();

    if (g_app_mode == AppMode::RADAR) {
      if (g_radar_ctrl == RadarControlMode::ZOOM) {
        onRangeStep(is_cw);
      } else {
        // In SELECT mode: cycle through aircraft 0 .. n-1, and index n (-1) is back to zoom
        g_last_select_action_ms = millis();
        if (n > 0) {
          int current = (g_selected_aircraft_idx < 0)
                            ? static_cast<int>(n)
                            : g_selected_aircraft_idx;
          if (is_cw) {
            current = (current + 1) % (n + 1);
          } else {
            current = (current + n) % (n + 1);
          }
          g_selected_aircraft_idx =
              (current == static_cast<int>(n)) ? -1 : current;
          showCurrentScreen();
        }
      }
    } else if (g_app_mode == AppMode::DETAIL) {
      // In DETAIL mode: knob directly flips to next/previous aircraft!
      if (n > 0) {
        if (is_cw) {
          g_selected_aircraft_idx = (g_selected_aircraft_idx + 1) % n;
        } else {
          g_selected_aircraft_idx = (g_selected_aircraft_idx + n - 1) % n;
        }
        showCurrentScreen();
      }
    } else if (g_app_mode == AppMode::CLOCK) {
      if (is_cw) {
        ui::clockDisplayNextTheme();
      } else {
        ui::clockDisplayPrevTheme();
      }
      ui::clockDisplayDraw(false);
    }
  }

  const ButtonAction act = hardware::encoder::pollButtonAction();
  if (act == ButtonAction::DOUBLE_CLICK) {
    // Double click toggles between RADAR and CLOCK from any screen!
    if (g_app_mode == AppMode::CLOCK) {
      g_app_mode = AppMode::RADAR;
      g_last_adsb_fetch_ms = 0;  // Immediately fetch fresh aircraft
      Serial.println("Mode: RADAR (double click)");
    } else {
      g_app_mode = AppMode::CLOCK;
      Serial.println("Mode: CLOCK (double click)");
    }
    showCurrentScreen();
  } else if (act == ButtonAction::CLICK) {
    const size_t n = services::adsb::aircraftCount();

    if (g_app_mode == AppMode::CLOCK) {
      ui::clockDisplayNextTheme();
      ui::clockDisplayDraw(false);
    } else if (g_app_mode == AppMode::DETAIL) {
      // Click in detail returns back to radar (in SELECT mode)
      g_app_mode = AppMode::RADAR;
      g_radar_ctrl = RadarControlMode::SELECT;
      g_last_select_action_ms = millis();
      Serial.println("Detail -> Radar");
      showCurrentScreen();
    } else if (g_app_mode == AppMode::RADAR) {
      if (g_radar_ctrl == RadarControlMode::ZOOM) {
        // Single click toggles to SELECT mode (starting at "STISK = ZPET" so another click returns to ZOOM)
        g_radar_ctrl = RadarControlMode::SELECT;
        g_selected_aircraft_idx = -1;
        g_last_select_action_ms = millis();
        Serial.println("Radar: ZOOM -> SELECT mode");
        showCurrentScreen();
      } else {
        // In SELECT mode:
        g_last_select_action_ms = millis();
        if (g_selected_aircraft_idx >= 0 &&
            g_selected_aircraft_idx < static_cast<int>(n)) {
          // Open DETAIL of selected aircraft!
          g_app_mode = AppMode::DETAIL;
          Serial.printf("Radar -> DETAIL (%s)\n",
                        services::adsb::aircraftList()[g_selected_aircraft_idx]
                            .callsign);
          showCurrentScreen();
        } else {
          // Click while on [ VYBER LETADLA (STISK=ZPET) ] -> returns to ZOOM!
          g_radar_ctrl = RadarControlMode::ZOOM;
          g_selected_aircraft_idx = -1;
          Serial.println("Radar: SELECT -> ZOOM mode");
          showCurrentScreen();
        }
      }
    }
  } else if (act == ButtonAction::LONG_PRESS) {
    // Long press immediately returns to ZOOM mode from anywhere
    if (g_app_mode == AppMode::DETAIL) {
      g_app_mode = AppMode::RADAR;
      g_radar_ctrl = RadarControlMode::ZOOM;
      g_selected_aircraft_idx = -1;
      showCurrentScreen();
    } else if (g_app_mode == AppMode::RADAR &&
               g_radar_ctrl == RadarControlMode::SELECT) {
      g_radar_ctrl = RadarControlMode::ZOOM;
      g_selected_aircraft_idx = -1;
      showCurrentScreen();
    }
  }
}

void appPoll() {
  handleEncoder();
  handleBootButton();
  wifiLoop();
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    handleEncoder();
    return;
  }

  if (g_app_mode == AppMode::RADAR) {
    ui::radarSetSelection(g_radar_ctrl == RadarControlMode::SELECT,
                          g_selected_aircraft_idx);
    ui::radarDisplayRefreshAircraft();
  } else if (g_app_mode == AppMode::DETAIL) {
    showCurrentScreen();
  }

  handleBootButton();
  handleEncoder();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar, Target Telemetry & Aviator Chronometer");

  bootButtonInit();
  hardware::encoder::init();
  displayInit();
  ui::clockDisplayInit();
  ui::aircraftDetailInit();

  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(appPoll);

  if (wifiSetupConnect()) {
    showCurrentScreen();
  }
}

void loop() {
  handleBootButton();
  handleEncoder();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_screen_ready) {
      Serial.println("WiFi lost — will reconnect");
      g_screen_ready = false;
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
        showCurrentScreen();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_screen_ready) {
      showCurrentScreen();
    } else {
      if (g_app_mode == AppMode::RADAR || g_app_mode == AppMode::DETAIL) {
        if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
          g_last_adsb_fetch_ms = millis();
          fetchAndDrawAircraft();
        }
      } else if (g_app_mode == AppMode::CLOCK) {
        // Redraw analog clock once per second for authentic chronograph tick
        time_t now_sec = time(nullptr);
        if (now_sec != g_last_clock_sec) {
          g_last_clock_sec = now_sec;
          ui::clockDisplayDraw(false);
        }
      }

      // Auto-timeout for SELECT mode: after 10 seconds of inactivity, return to ZOOM mode
      if (g_app_mode == AppMode::RADAR &&
          g_radar_ctrl == RadarControlMode::SELECT) {
        if (millis() - g_last_select_action_ms >= 10000) {
          Serial.println("SELECT mode timeout (10s) -> ZOOM mode");
          g_radar_ctrl = RadarControlMode::ZOOM;
          g_selected_aircraft_idx = -1;
          showCurrentScreen();
        }
      }
    }
  }

  delay(10);
}
