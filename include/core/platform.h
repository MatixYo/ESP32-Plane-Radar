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

struct HttpClient {
  /** Blocking GET of the whole body. Returns false on any transport error. */
  static bool get(const char* url, std::string* out, unsigned long timeout_ms,
                  PollFn poll);
};

}  // namespace core::platform
