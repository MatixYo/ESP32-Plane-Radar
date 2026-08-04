#pragma once

#include <cstdint>

namespace hardware::display {

constexpr uint8_t spiModeForBoard(bool is_nm_tv_154) {
  return is_nm_tv_154 ? 3 : 0;
}

}  // namespace hardware::display
