/**
 * Device HttpClient: Arduino HTTPClient over WiFiClientSecure.
 *
 * Both polling loops below exist because a request can block for seconds, and
 * the config portal and the BOOT button have to stay alive across it — hence
 * the PollFn invoked on every iteration of the connect loop and of the refill
 * that feeds the body reader.
 *
 * TLS is deliberately unverified (setInsecure), matching the shipping firmware.
 */

#include "core/platform.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstring>

namespace core::platform {

namespace {

constexpr int kConnectAttemptMs = 200;
constexpr size_t kReadChunk = 512;

void poll(PollFn fn) {
  if (fn != nullptr) {
    fn();
  }
}

int performGetWithPoll(HTTPClient& http, unsigned long timeout_ms, PollFn fn) {
  http.setConnectTimeout(kConnectAttemptMs);
  const unsigned long deadline = millis() + timeout_ms;
  while (millis() < deadline) {
    poll(fn);
    const int code = http.GET();
    if (code > 0) {
      return code;
    }
    if (code != HTTPC_ERROR_CONNECTION_REFUSED &&
        code != HTTPC_ERROR_NOT_CONNECTED) {
      return code;
    }
    delay(5);
  }
  return HTTPC_ERROR_READ_TIMEOUT;
}

/**
 * Reads the body straight off the socket, one kReadChunk window at a time.
 *
 * The window is the only buffer in the path — the decoder consumes bytes as
 * fast as they arrive, so peak RAM does not grow with the size of the reply.
 * The poll loop around the refill is what the old read loop did: a request can
 * block for seconds and the portal and BOOT button have to survive it.
 */
class StreamBodyReader : public BodyReader {
 public:
  StreamBodyReader(HTTPClient& http, WiFiClient& stream, unsigned long deadline,
                   PollFn fn)
      : http_(http),
        stream_(stream),
        remaining_(http.getSize()),
        deadline_(deadline),
        poll_fn_(fn) {}

  int read() override {
    if (pos_ >= len_ && !fill()) {
      return -1;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

  size_t readBytes(char* buf, size_t len) override {
    size_t written = 0;
    while (written < len) {
      if (pos_ >= len_ && !fill()) {
        break;
      }
      const size_t take = len_ - pos_ < len - written ? len_ - pos_
                                                      : len - written;
      memcpy(buf + written, buffer_ + pos_, take);
      pos_ += take;
      written += take;
    }
    return written;
  }

 private:
  /** Blocks until at least one byte lands, the body ends, or time runs out. */
  bool fill() {
    pos_ = 0;
    len_ = 0;
    // A negative Content-Length means the server did not send one; then the
    // close of the connection is the only end-of-body marker there is.
    if (remaining_ == 0) {
      return false;
    }

    while (millis() < deadline_) {
      poll(poll_fn_);
      const int available = stream_.available();
      if (available > 0) {
        size_t want = sizeof(buffer_);
        if (remaining_ > 0 && static_cast<size_t>(remaining_) < want) {
          want = static_cast<size_t>(remaining_);
        }
        const size_t got = stream_.readBytes(buffer_, want);
        if (got > 0) {
          len_ = got;
          if (remaining_ > 0) {
            remaining_ -= static_cast<int>(got);
          }
          return true;
        }
      }
      if (!http_.connected() && stream_.available() <= 0) {
        return false;
      }
      delay(1);
    }
    return false;
  }

  HTTPClient& http_;
  WiFiClient& stream_;
  int remaining_;
  unsigned long deadline_;
  PollFn poll_fn_;
  char buffer_[kReadChunk];
  size_t pos_ = 0;
  size_t len_ = 0;
};

}  // namespace

bool HttpClient::get(const char* url, BodyFn on_body, unsigned long timeout_ms,
                     PollFn fn) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    logf("http: begin failed\n");
    return false;
  }

  http.setTimeout(timeout_ms);
  const int code = performGetWithPoll(http, timeout_ms, fn);
  if (code != HTTP_CODE_OK) {
    logf("http: HTTP %d\n", code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    return false;
  }

  StreamBodyReader body(http, *stream, millis() + timeout_ms, fn);
  const bool ok = on_body(body);
  http.end();
  return ok;
}

}  // namespace core::platform
