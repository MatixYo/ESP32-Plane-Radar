/**
 * Native font blob: read data/ui_font.vlw from disk once.
 *
 * The buffer is a function-local static returned by reference and is never
 * resized after the first load. This is load-bearing: LGFXBase::loadFont()
 * stores the pointer and reads from it lazily for the lifetime of the program,
 * so a local or reallocated buffer would leave every textWidth()/drawString()
 * call reading freed memory — and it would usually appear to work, which is the
 * worst possible failure mode for a harness whose entire purpose is faithful
 * font metrics.
 */

#include "core/platform.h"

#include <cstdio>
#include <vector>

#ifndef PLANE_RADAR_FONT_PATH
#define PLANE_RADAR_FONT_PATH "data/ui_font.vlw"
#endif

namespace core::platform {

namespace {

const std::vector<uint8_t>& blob() {
  static const std::vector<uint8_t> data = [] {
    std::vector<uint8_t> out;
    std::FILE* f = std::fopen(PLANE_RADAR_FONT_PATH, "rb");
    if (f == nullptr) {
      logf("font: cannot open %s\n", PLANE_RADAR_FONT_PATH);
      return out;
    }
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len > 0) {
      out.resize(static_cast<size_t>(len));
      if (std::fread(out.data(), 1, out.size(), f) != out.size()) {
        logf("font: short read from %s\n", PLANE_RADAR_FONT_PATH);
        out.clear();
      }
    }
    std::fclose(f);
    return out;
  }();
  return data;
}

}  // namespace

const uint8_t* fontBlobData() { return blob().empty() ? nullptr : blob().data(); }

size_t fontBlobLen() { return blob().size(); }

}  // namespace core::platform
