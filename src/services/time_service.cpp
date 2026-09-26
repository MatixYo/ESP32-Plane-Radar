#include "services/time_service.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include "config.h"

namespace services::time {

namespace {

bool s_initialized = false;

void timeSyncCallback(struct timeval* tv) {
  Serial.println("time: NTP sync completed");
}

}  // namespace

void init() {
  if (s_initialized) {
    return;
  }
  sntp_set_time_sync_notification_cb(timeSyncCallback);
  configTzTime(config::kNtpTimezone, config::kNtpServer1, config::kNtpServer2);
  s_initialized = true;
  Serial.println("time: NTP service initialized");
}

bool isSynced() {
  time_t now = 0;
  ::time(&now);
  return now > 1704067200;  // Past 2024
}

bool getLocalTimeInfo(struct tm* info) {
  time_t now = 0;
  ::time(&now);
  if (now < 1704067200) {
    return false;
  }
  localtime_r(&now, info);
  return true;
}

}  // namespace services::time
