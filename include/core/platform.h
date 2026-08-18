#pragma once

/**
 * The portability seam.
 *
 * Everything the shared code needs from the outside world — clock, logging,
 * reboot, persistent storage, HTTP, the embedded font, and the portal
 * instructions shown on the setup screen — is declared here and implemented
 * once per destination under src/platform/{device,native}/.
 *
 * This header must stay free of Arduino, ESP-IDF and LovyanGFX includes.
 */

#include <cstddef>
#include <cstdint>
#include <string>

namespace core::platform {

// --- Startup -----------------------------------------------------------------

/** Bring up the logging transport (device: Serial at 115200). */
void logInit();

// --- Clock -------------------------------------------------------------------

/** Milliseconds since boot. Monotonic. */
unsigned long nowMs();

/** Yield for at least `ms`. Must be a real sleep, not a spin. */
void sleepMs(unsigned long ms);

// --- Logging -----------------------------------------------------------------

/** printf semantics — no implicit newline, matching Serial.printf. */
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// --- Reboot ------------------------------------------------------------------

/**
 * Restart the program. Never returns.
 *
 * Device: esp_restart(). Native: exit(0) — deliberately NOT an in-process
 * re-entry of setup(), because the guard statics in radar_display.cpp
 * (s_frame_ready, s_label_metrics_ready, s_tag_label_metrics_ready) and
 * display_font.cpp (s_vlw_loaded) would survive it, so the "reboot" would stop
 * reproducing a cold boot. It is also reached from inside a nested
 * loop() -> wifiLoop() -> bootButtonPollLongPress() call chain, which must not
 * be allowed to recurse.
 */
[[noreturn]] void reboot();

// --- Embedded font -----------------------------------------------------------

/**
 * The VLW smooth-font blob.
 *
 * NOTE: LGFXBase::loadFont() RETAINS this pointer (it does _font_data.set(array)
 * and reads lazily), so the storage must outlive the program. The device backs
 * this with linker symbols; the native backend must use an immortal buffer.
 */
const uint8_t* fontBlobData();
size_t fontBlobLen();

// --- Setup-screen instructions ----------------------------------------------

/** What the portal status screen tells the user to connect to. */
struct PortalHints {
  const char* join;  ///< AP name to join, e.g. "PlaneRadar-Setup"
  const char* url;   ///< primary URL, e.g. "plane-radar.local"
  const char* alt;   ///< fallback line, e.g. "or 192.168.4.1"
};
PortalHints portalHints();

// --- Persistent key/value storage -------------------------------------------

/**
 * Namespaced key/value storage (device: NVS via Preferences).
 *
 * The namespace is a call parameter and every operation opens and closes its
 * own handle. Both are deliberate: the radar location and the range/units
 * settings live in separate NVS namespaces ("radar" and "planeradar") to avoid
 * NVS handle conflicts, and holding a handle open across calls is what makes
 * those conflicts possible.
 */
struct KeyValueStore {
  static bool has(const char* ns, const char* key);
  static void remove(const char* ns, const char* key);

  static bool getBool(const char* ns, const char* key, bool def);
  static void putBool(const char* ns, const char* key, bool value);

  static uint8_t getU8(const char* ns, const char* key, uint8_t def);
  static void putU8(const char* ns, const char* key, uint8_t value);

  static double getDouble(const char* ns, const char* key, double def);
  static void putDouble(const char* ns, const char* key, double value);

  static std::string getString(const char* ns, const char* key, const char* def);
  static void putString(const char* ns, const char* key, const char* value);
};

// --- HTTP --------------------------------------------------------------------

/**
 * Cooperative poll hook, invoked during long HTTP I/O.
 *
 * main.cpp wires this to wifiLoop() so the config portal and the BOOT button
 * stay responsive across a request. Dropping it would leave the portal dead for
 * the duration of every fetch.
 */
using PollFn = void (*)();

/**
 * Pull-based view of a response body.
 *
 * The body is never held in RAM as a whole: an adsb.fi reply is tens of
 * kilobytes at the wider ranges, and a single allocation that size throws
 * std::bad_alloc on the device, which aborts the firmware. read() and
 * readBytes() have the signatures ArduinoJson's deserializer expects, so a
 * reference to one of these can be passed straight to deserializeJson().
 */
struct BodyReader {
  virtual ~BodyReader() = default;

  /** Next byte, or -1 once the body is exhausted. */
  virtual int read() = 0;

  /** Fills up to `len` bytes; a short count means end of body. */
  virtual size_t readBytes(char* buf, size_t len) = 0;
};

/** BodyReader over bytes already in RAM — the native transport and tests. */
class MemoryBodyReader : public BodyReader {
 public:
  MemoryBodyReader(const char* data, size_t len) : data_(data), len_(len) {}

  int read() override {
    if (pos_ >= len_) {
      return -1;
    }
    return static_cast<unsigned char>(data_[pos_++]);
  }

  size_t readBytes(char* buf, size_t len) override {
    const size_t left = len_ - pos_;
    const size_t n = len < left ? len : left;
    for (size_t i = 0; i < n; ++i) {
      buf[i] = data_[pos_++];
    }
    return n;
  }

 private:
  const char* data_;
  size_t len_;
  size_t pos_ = 0;
};

/**
 * Decodes a response body. Returning false marks the whole request failed.
 *
 * It runs while the connection is still open, so it must not block longer than
 * the request timeout allows.
 */
using BodyFn = bool (*)(BodyReader& body);

struct HttpClient {
  /**
   * Blocking GET that hands the body to `on_body` as it arrives. Returns false
   * on any transport error, on a non-200 status, or if `on_body` does.
   */
  static bool get(const char* url, BodyFn on_body, unsigned long timeout_ms,
                  PollFn poll);
};

}  // namespace core::platform
