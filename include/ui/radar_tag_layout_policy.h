/*
 * SPDX-FileCopyrightText: 2026 RockBase IoT (Chengdu) CO., LTD.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

namespace ui::radar {

struct TagRect {
  int left;
  int top;
  int right;
  int bottom;
};

constexpr bool tagRectInside(TagRect rect, int width, int height) {
  return rect.left >= 0 && rect.top >= 0 && rect.right <= width &&
         rect.bottom <= height && rect.left < rect.right && rect.top < rect.bottom;
}

constexpr bool tagRectsOverlap(TagRect first, TagRect second) {
  return first.left < second.right && first.right > second.left &&
         first.top < second.bottom && first.bottom > second.top;
}

}  // namespace ui::radar