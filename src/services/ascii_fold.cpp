#include "services/ascii_fold.h"

#include <cstdint>

namespace services {

namespace {

constexpr uint32_t kFoldFirst = 0xC0;
constexpr uint32_t kFoldLast = 0x17F;
const char* const kFoldTable[] = {
    "A", "A", "A", "A", "A", "A", "AE", "C",
    "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "O", "",
    "O", "U", "U", "U", "U", "Y", "Th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c",
    "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "",
    "o", "u", "u", "u", "u", "y", "th", "y",
    "A", "a", "A", "a", "A", "a", "C", "c",
    "C", "c", "C", "c", "C", "c", "D", "d",
    "D", "d", "E", "e", "E", "e", "E", "e",
    "E", "e", "E", "e", "G", "g", "G", "g",
    "G", "g", "G", "g", "H", "h", "H", "h",
    "I", "i", "I", "i", "I", "i", "I", "i",
    "I", "i", "IJ", "ij", "J", "j", "K", "k",
    "k", "L", "l", "L", "l", "L", "l", "L",
    "l", "L", "l", "N", "n", "N", "n", "N",
    "n", "n", "N", "n", "O", "o", "O", "o",
    "O", "o", "OE", "oe", "R", "r", "R", "r",
    "R", "r", "S", "s", "S", "s", "S", "s",
    "S", "s", "T", "t", "T", "t", "T", "t",
    "U", "u", "U", "u", "U", "u", "U", "u",
    "U", "u", "U", "u", "W", "w", "Y", "y",
    "Y", "Z", "z", "Z", "z", "Z", "z", "s",
};

}  // namespace

bool foldToAscii(const char* in, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return false;
  }
  out[0] = '\0';
  if (in == nullptr) {
    return false;
  }

  size_t o = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in);
  while (*p != '\0') {
    uint32_t cp = 0;
    if (*p < 0x80) {
      cp = *p++;
    } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      cp = (static_cast<uint32_t>(*p & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else {
      out[0] = '\0';
      return false;
    }

    char single[2] = {0, 0};
    const char* rep = nullptr;
    if (cp >= 0x20 && cp < 0x7F) {
      single[0] = static_cast<char>(cp);
      rep = single;
    } else if (cp >= kFoldFirst && cp <= kFoldLast) {
      rep = kFoldTable[cp - kFoldFirst];
    }
    if (rep == nullptr || rep[0] == '\0') {
      out[0] = '\0';
      return false;
    }
    for (; *rep != '\0' && o + 1 < out_len; ++rep) {
      out[o++] = *rep;
    }
  }
  out[o] = '\0';
  return o > 0;
}

}  // namespace services
