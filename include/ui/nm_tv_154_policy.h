#pragma once

namespace ui::nm_tv_154 {

struct CornerTelemetryLayout {
  int top_label_y;
  int top_value_y;
  int bottom_label_y;
  int bottom_value_y;
};

constexpr CornerTelemetryLayout cornerTelemetryLayout(int display_size, int inset,
                                                       int label_height,
                                                       int value_height, int gap) {
  return {inset, inset + label_height + gap,
          display_size - inset - value_height - gap - label_height,
          display_size - inset};
}

constexpr bool cornerTelemetryLayoutFits(int display_size, int inset,
                                         int label_height, int value_height,
                                         int gap) {
  if (display_size <= 0 || inset < 0 || label_height <= 0 || value_height <= 0 ||
      gap < 0) {
    return false;
  }

  const CornerTelemetryLayout layout =
      cornerTelemetryLayout(display_size, inset, label_height, value_height, gap);
  return layout.top_label_y >= inset &&
         layout.top_label_y + label_height + gap <= layout.top_value_y &&
         layout.top_value_y + value_height + gap <= layout.bottom_label_y &&
         layout.bottom_label_y + label_height + gap <=
             layout.bottom_value_y - value_height &&
         layout.bottom_value_y <= display_size - inset;
}

struct TouchRangeState {
  bool was_down = false;
  bool range_tap = false;
};

constexpr TouchRangeState nextTouchRangeState(TouchRangeState state, bool is_down) {
  state.range_tap = false;
  if (is_down) {
    state.was_down = true;
    return state;
  }
  if (!state.was_down) {
    return state;
  }

  state.was_down = false;
  state.range_tap = true;
  return state;
}

}  // namespace ui::nm_tv_154