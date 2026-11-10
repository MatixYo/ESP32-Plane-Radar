#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

struct SplashPhoto {
  const uint8_t* data;
  size_t len;
};

inline constexpr size_t kSplashPhotoCount = 6;
extern const SplashPhoto kSplashPhotos[kSplashPhotoCount];

}  // namespace ui