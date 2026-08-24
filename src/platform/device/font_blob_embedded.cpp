/**
 * Device font blob: the VLW is embedded into flash by PlatformIO's
 * board_build.embed_files, which emits these two linker symbols. The storage is
 * in flash and immortal, which satisfies loadFont()'s pointer-retention
 * requirement for free.
 */

#include "core/platform.h"

extern "C" {
extern const uint8_t _binary_data_ui_font_vlw_start[] asm(
    "_binary_data_ui_font_vlw_start");
extern const uint8_t _binary_data_ui_font_vlw_end[] asm(
    "_binary_data_ui_font_vlw_end");
}

namespace core::platform {

const uint8_t* fontBlobData() { return _binary_data_ui_font_vlw_start; }

size_t fontBlobLen() {
  return static_cast<size_t>(_binary_data_ui_font_vlw_end -
                             _binary_data_ui_font_vlw_start);
}

}  // namespace core::platform
