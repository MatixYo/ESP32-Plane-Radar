#pragma once

#include <cstdint>

namespace ui::radar {

constexpr bool frameSpriteEnabledForBoard(bool) {
  return true;
}

constexpr uint8_t frameSpriteColorDepthForBoard(bool is_nm_tv_154) {
  return is_nm_tv_154 ? 8 : 16;
}

}  // namespace ui::radar
