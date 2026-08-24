/** Native implementations of the clock, logging, reboot and portal-hint seam. */

#include "core/platform.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace core::platform {

namespace {

std::chrono::steady_clock::time_point startPoint() {
  static const auto start = std::chrono::steady_clock::now();
  return start;
}

}  // namespace

void logInit() {
  // stdout is the transport; make it line-buffered so output interleaves
  // sensibly with the SDL window's own logging.
  setvbuf(stdout, nullptr, _IOLBF, 0);
  startPoint();
}

unsigned long nowMs() {
  const auto delta = std::chrono::steady_clock::now() - startPoint();
  return static_cast<unsigned long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());
}

void sleepMs(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void logf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void reboot() {
  // A real process exit, not an in-process restart: see the rationale on the
  // declaration in core/platform.h.
  std::fflush(stdout);
  std::exit(0);
}

PortalHints portalHints() {
  return PortalHints{"(native harness — no AP)", "127.0.0.1:8080",
                     "SPACE = BOOT button"};
}

}  // namespace core::platform
