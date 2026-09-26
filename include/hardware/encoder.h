#pragma once

namespace hardware::encoder {

enum class Rotation {
  NONE = 0,
  CW,   // Clockwise (doprava / zoom out)
  CCW   // Counter-clockwise (doleva / zoom in)
};

enum class ButtonAction {
  NONE = 0,
  CLICK,
  DOUBLE_CLICK,
  LONG_PRESS
};

void init();
Rotation pollRotation();
ButtonAction pollButtonAction();
bool pollButtonClick();

}  // namespace hardware::encoder
