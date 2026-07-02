#pragma once

#include <cstdint>

// Board support: maps a target board to its GC9A01 display wiring. The board
// can be chosen at compile time (-DBOARD_ESP32_2424S012, see platformio.ini)
// and overridden at runtime from the Wi-Fi setup portal (persisted in NVS,
// applied on the next boot).
namespace hardware::board {

enum class Board : uint8_t {
  kSuperMini = 0,       // ESP32-C3 Super Mini + separately wired GC9A01
  kEsp32_2424S012 = 1,  // Sunton/JCZN integrated 1.28" round board
};
constexpr uint8_t kBoardCount = 2;

/** Board-dependent display wiring. Pins are GPIO numbers; -1 = not wired. */
struct DisplayPins {
  int pin_sclk;
  int pin_mosi;
  int pin_dc;
  int pin_cs;
  int pin_rst;        // -1 = tied to chip reset (not wired)
  int pin_backlight;  // -1 = hard-wired / not GPIO-controlled
  bool rgb_order;     // true = RGB, false = BGR
  // Capacitive touch (CST816, I2C). touch_sda < 0 means no touch on this board.
  int touch_sda;
  int touch_scl;
  int touch_int;  // -1 if not wired
  int touch_rst;  // -1 if not wired
};

inline constexpr DisplayPins kBoards[kBoardCount] = {
    // kSuperMini: ESP32-C3 Super Mini with a separately wired GC9A01 (no touch).
    {/*sclk*/ 4, /*mosi*/ 3, /*dc*/ 10, /*cs*/ 1, /*rst*/ 0, /*bl*/ -1,
     /*rgb_order*/ true,
     /*touch sda*/ -1, /*scl*/ -1, /*int*/ -1, /*rst*/ -1},
    // kEsp32_2424S012: Sunton/JCZN integrated round board (BL on GPIO3, BGR,
    // CST816 touch on I2C).
    {/*sclk*/ 6, /*mosi*/ 7, /*dc*/ 2, /*cs*/ 10, /*rst*/ -1, /*bl*/ 3,
     /*rgb_order*/ false,
     /*touch sda*/ 4, /*scl*/ 5, /*int*/ 0, /*rst*/ 1},
};

constexpr Board compileDefault() {
#if defined(BOARD_ESP32_2424S012)
  return Board::kEsp32_2424S012;
#else
  return Board::kSuperMini;
#endif
}

constexpr const DisplayPins& pins(Board b) {
  return kBoards[static_cast<uint8_t>(b)];
}

constexpr bool isValidIndex(long v) { return v >= 0 && v < kBoardCount; }

const char* name(Board b);

// Runtime-selected board: NVS override if set and valid, else compileDefault().
Board active();
const DisplayPins& activePins();
void setActive(Board b);  // persists to NVS; takes effect on next boot

}  // namespace hardware::board
