/**
 * Native HttpClient: libcurl easy interface.
 *
 * Three things here differ from the device backend on purpose:
 *
 *  - The timeout is capped well below what the caller asks for. adsb.cpp passes
 *    10000 ms, which is the device's worst case over flaky WiFi; on the host a
 *    stall that long freezes the whole single-threaded harness — the config
 *    portal and the keyboard BOOT button go dead — because this call blocks
 *    loop(). We would rather truncate a slow fetch and retry on the next cycle.
 *  - The PollFn is wired into curl's progress callback rather than a hand-rolled
 *    read loop, so it still runs while the transfer is in flight. It is
 *    load-bearing: without it the portal and BOOT button are unresponsive for
 *    the duration of every request, which is every 3 seconds.
 *  - TLS certificate verification is left ON. The device does setInsecure()
 *    because it has no CA bundle and no wall clock; the host has both, so do
 *    NOT "align" this with the device by disabling verification.
 */

#include "core/platform.h"

#include <curl/curl.h>

#include <algorithm>
#include <string>

namespace core::platform {

namespace {

/**
 * Upper bound on a single request, regardless of what the caller asked for.
 * See the file comment: this blocks loop(), so a long stall is a frozen UI.
 */
constexpr long kMaxTimeoutMs = 2500;
constexpr long kConnectTimeoutMs = 2000;
constexpr char kUserAgent[] = "plane-radar-native";

/** curl_global_init is not reentrant; a function-local static serialises it. */
void ensureGlobalInit() {
  static const CURLcode s_init = curl_global_init(CURL_GLOBAL_DEFAULT);
  (void)s_init;  // Nothing useful to do on failure; curl_easy_init will fail.
}

size_t writeToString(char* data, size_t size, size_t nmemb, void* userdata) {
  const size_t total = size * nmemb;
  static_cast<std::string*>(userdata)->append(data, total);
  return total;
}

/**
 * Runs on curl's progress ticks, i.e. while the socket is idle mid-transfer.
 * This is the only place the harness gets control back during a request.
 * Returning non-zero would abort the transfer, so always return 0.
 */
int pumpPoll(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  PollFn fn = reinterpret_cast<PollFn>(userdata);
  if (fn != nullptr) {
    fn();
  }
  return 0;
}

/** Closes the easy handle on every exit path, including the error returns. */
class EasyHandle {
 public:
  EasyHandle() : handle_(curl_easy_init()) {}
  ~EasyHandle() {
    if (handle_ != nullptr) {
      curl_easy_cleanup(handle_);
    }
  }

  EasyHandle(const EasyHandle&) = delete;
  EasyHandle& operator=(const EasyHandle&) = delete;

  CURL* get() const { return handle_; }

 private:
  CURL* handle_;
};

}  // namespace

bool HttpClient::get(const char* url, std::string* out,
                     unsigned long timeout_ms, PollFn fn) {
  out->clear();

  ensureGlobalInit();

  EasyHandle easy;
  if (easy.get() == nullptr) {
    logf("http: curl_easy_init failed\n");
    return false;
  }
  CURL* curl = easy.get();

  const long timeout =
      std::min(static_cast<long>(timeout_ms), kMaxTimeoutMs);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  // Empty string = accept every encoding this build of curl can decode, so the
  // caller always sees plain JSON.
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  // Without this curl installs a SIGALRM handler for its DNS timeout, which
  // would land in the middle of the SDL event loop.
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  // NOPROGRESS must be cleared or the transfer callback is never invoked.
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, pumpPoll);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, reinterpret_cast<void*>(fn));

  const CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    logf("http: %s\n", curl_easy_strerror(res));
    out->clear();  // A partial body is worse than none; the caller retries.
    return false;
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (status != 200) {
    logf("http: HTTP %ld\n", status);
    out->clear();
    return false;
  }

  return true;
}

}  // namespace core::platform
