/**
 * Device HttpClient: Arduino HTTPClient over WiFiClientSecure.
 *
 * The two polling loops below are moved verbatim from adsb_client.cpp. They
 * exist because a request can block for seconds, and the config portal and the
 * BOOT button have to stay alive across it — hence the PollFn invoked on every
 * iteration of both the connect loop and the body-read loop.
 *
 * TLS is deliberately unverified (setInsecure), matching the shipping firmware.
 */

#include "core/platform.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

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

bool readBodyWithPoll(HTTPClient& http, std::string* out,
                      unsigned long timeout_ms, PollFn fn) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0) {
    out->reserve(static_cast<size_t>(content_length) + 1);
  }

  uint8_t buffer[kReadChunk];
  const unsigned long deadline = millis() + timeout_ms;
  while (millis() < deadline) {
    poll(fn);
    const int available = stream->available();
    if (available > 0) {
      const int to_read = available > static_cast<int>(sizeof(buffer))
                              ? static_cast<int>(sizeof(buffer))
                              : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        out->append(reinterpret_cast<const char*>(buffer),
                    static_cast<size_t>(read_bytes));
      }
    }
    if (content_length > 0 &&
        static_cast<int>(out->size()) >= content_length) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return !out->empty();
}

}  // namespace

bool HttpClient::get(const char* url, std::string* out,
                     unsigned long timeout_ms, PollFn fn) {
  out->clear();

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

  const bool ok = readBodyWithPoll(http, out, timeout_ms, fn);
  http.end();
  return ok;
}

}  // namespace core::platform
