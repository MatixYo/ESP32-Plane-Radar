#pragma once

namespace ui::radar {

constexpr bool frameSpriteEnabledForBoard(bool is_nm_tv_154) {
  return !is_nm_tv_154;
}

}  // namespace ui::radar
