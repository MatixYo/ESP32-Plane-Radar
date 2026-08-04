#pragma once

#include <cstdint>

namespace ui::square {

enum class UpdateFreshness : uint8_t {
  fresh,
  stale,
  unavailable,
};

constexpr uint8_t wifiBars(bool connected, int rssi) {
  if (!connected) {
    return 0;
  }
  if (rssi >= -55) {
    return 4;
  }
  if (rssi >= -67) {
    return 3;
  }
  if (rssi >= -75) {
    return 2;
  }
  return 1;
}

constexpr unsigned long elapsedMs(unsigned long now_ms,
                                  unsigned long then_ms) {
  return now_ms - then_ms;
}

constexpr unsigned long updateAgeSeconds(unsigned long now_ms,
                                         unsigned long last_update_ms) {
  return elapsedMs(now_ms, last_update_ms) / 1000UL;
}

constexpr UpdateFreshness updateFreshness(bool connected, bool has_update,
                                          unsigned long now_ms,
                                          unsigned long last_update_ms) {
  if (!connected || !has_update) {
    return UpdateFreshness::unavailable;
  }

  const unsigned long age_ms = elapsedMs(now_ms, last_update_ms);
  if (age_ms < 10000UL) {
    return UpdateFreshness::fresh;
  }
  if (age_ms < 30000UL) {
    return UpdateFreshness::stale;
  }
  return UpdateFreshness::unavailable;
}

}  // namespace ui::square
