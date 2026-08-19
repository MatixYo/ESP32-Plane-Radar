/** Device implementations of the clock, logging, reboot and portal-hint seam. */

#include "core/platform.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cstdarg>
#include <cstdio>

#include "config.h"

namespace core::platform {

void logInit() {
  Serial.begin(115200);
  delay(500);
}

unsigned long nowMs() { return millis(); }

void sleepMs(unsigned long ms) { delay(ms); }

void logf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n > 0) {
    Serial.print(buf);
  }
}

void reboot() {
  delay(800);
  esp_restart();
  // esp_restart() does not return; satisfy [[noreturn]] for the compiler.
  while (true) {
  }
}

PortalHints portalHints() {
  return PortalHints{config::kPortalApName, config::kPortalHostUrl,
                     "or 192.168.4.1"};
}

}  // namespace core::platform
