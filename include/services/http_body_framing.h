#pragma once

#include <cstddef>
#include <cstdint>

namespace services::http {

/** How the response body is delimited (RFC 9112 section 6). */
enum class BodyFraming {
  /** Content-Length, or -- when the length is unknown -- read until close. */
  kIdentity,
  /** Transfer-Encoding: chunked. */
  kChunked,
};

/**
 * Unwraps an HTTP/1.1 message body from a raw byte source.
 *
 * `Source` supplies the body's wire image one byte at a time through
 *
 *     int read();  // next byte, or < 0 when no more is coming
 *
 * and this class hands back the decoded body, stripping chunk sizes, chunk
 * extensions, the inter-chunk CRLFs and any trailer section. Reading a byte at
 * a time is what makes the decoder correct: the framing state lives in the
 * object, so a chunk boundary landing anywhere in the source's own buffering
 * -- mid-hex-digit, or between the CR and the LF -- needs no special handling.
 * Callers that want throughput buffer inside the source, not out here.
 *
 * read()/readBytes() match the shape ArduinoJson expects of a custom input,
 * so an instance can be handed straight to deserializeJson().
 */
template <typename Source>
class BodyFramer {
 public:
  /**
   * `content_length` is the Content-Length value, or < 0 when the header was
   * absent. It is ignored for a chunked body, which carries its own lengths.
   */
  BodyFramer(Source& source, BodyFraming framing, int content_length)
      : source_(&source),
        framing_(framing),
        remaining_(framing == BodyFraming::kIdentity ? content_length : -1),
        state_(framing == BodyFraming::kChunked ? State::kSize
                                                : State::kBody) {}

  /** Next decoded body byte, or -1 once the body ends or the source dries
   *  up. */
  int read() {
    return framing_ == BodyFraming::kChunked ? readChunked() : readIdentity();
  }

  size_t readBytes(char* out, size_t length) {
    size_t n = 0;
    while (n < length) {
      const int c = read();
      if (c < 0) {
        break;
      }
      out[n++] = static_cast<char>(c);
    }
    return n;
  }

  /**
   * Consume whatever is left of the body, so the source is positioned at the
   * start of the next message. A parser stops at the closing brace and leaves
   * the terminating chunk (and any trailer) unread; a connection reused
   * without this would read that leftover as the next response.
   *
   * Does nothing for a close-delimited body, whose end only arrives when the
   * peer hangs up -- there is nothing to reuse in that case anyway.
   */
  void drain() {
    if (framing_ == BodyFraming::kIdentity && remaining_ < 0) {
      return;
    }
    while (read() >= 0) {
    }
  }

  /** Decoded body bytes handed out so far. */
  size_t bytesRead() const { return total_; }

  /** True once the body has been read through to its end. */
  bool complete() const { return state_ == State::kDone; }

  /** True when the chunk framing was malformed, rather than merely cut
   *  short. */
  bool framingError() const { return framing_error_; }

  /** True when the source ran dry before the body ended. */
  bool truncated() const { return truncated_; }

 private:
  enum class State {
    kBody,     // identity: body bytes
    kSize,     // chunked: hex digits of the chunk-size line
    kSizeEol,  // chunked: chunk extensions / CR, up to the line's LF
    kData,     // chunked: chunk payload
    kDataCR,   // chunked: CR of the CRLF that follows the payload
    kDataLF,   // chunked: LF of that CRLF
    kTrailer,  // chunked: trailer section, up to its blank line
    kDone,
    kError,
  };

  /** A chunk size is a 32-bit count; more digits than that is malformed. */
  static constexpr int kMaxSizeDigits = 8;

  static bool isHexDigit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  }

  static uint32_t hexValue(int c) {
    if (c >= '0' && c <= '9') {
      return static_cast<uint32_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<uint32_t>(c - 'a' + 10);
    }
    return static_cast<uint32_t>(c - 'A' + 10);
  }

  int malformed() {
    framing_error_ = true;
    state_ = State::kError;
    return -1;
  }

  int cutShort() {
    truncated_ = true;
    state_ = State::kError;
    return -1;
  }

  int readIdentity() {
    if (state_ != State::kBody) {
      return -1;
    }
    if (remaining_ == 0) {
      state_ = State::kDone;
      return -1;
    }
    const int c = source_->read();
    if (c < 0) {
      // Without a Content-Length the close *is* the terminator; with one, a
      // short read means the body was cut off.
      if (remaining_ < 0) {
        state_ = State::kDone;
        return -1;
      }
      return cutShort();
    }
    if (remaining_ > 0) {
      --remaining_;
    }
    ++total_;
    return c;
  }

  /** Chunk-size line is parsed; decide what follows it. */
  void finishSizeLine() {
    if (chunk_remaining_ == 0) {
      trailer_line_len_ = 0;
      state_ = State::kTrailer;
    } else {
      state_ = State::kData;
    }
  }

  int readChunked() {
    for (;;) {
      switch (state_) {
        case State::kSize: {
          const int c = source_->read();
          if (c < 0) {
            return cutShort();
          }
          if (isHexDigit(c)) {
            if (size_digits_ >= kMaxSizeDigits) {
              return malformed();
            }
            chunk_remaining_ = chunk_remaining_ * 16 + hexValue(c);
            ++size_digits_;
            break;
          }
          if (size_digits_ == 0) {
            return malformed();  // a chunk-size line must start with hex
          }
          if (c == ';' || c == '\r') {
            state_ = State::kSizeEol;  // chunk extensions, then the CRLF
            break;
          }
          if (c == '\n') {
            finishSizeLine();
            break;
          }
          return malformed();
        }

        case State::kSizeEol: {
          const int c = source_->read();
          if (c < 0) {
            return cutShort();
          }
          if (c == '\n') {
            finishSizeLine();
          }
          break;  // anything else is extension text, discarded
        }

        case State::kData: {
          const int c = source_->read();
          if (c < 0) {
            return cutShort();
          }
          if (--chunk_remaining_ == 0) {
            state_ = State::kDataCR;
          }
          ++total_;
          return c;
        }

        case State::kDataCR: {
          const int c = source_->read();
          if (c < 0) {
            return cutShort();
          }
          if (c == '\r') {
            state_ = State::kDataLF;
            break;
          }
          if (c == '\n') {  // tolerate a bare LF between chunks
            startSizeLine();
            break;
          }
          return malformed();
        }

        case State::kDataLF: {
          const int c = source_->read();
          if (c < 0) {
            return cutShort();
          }
          if (c != '\n') {
            return malformed();
          }
          startSizeLine();
          break;
        }

        case State::kTrailer: {
          const int c = source_->read();
          if (c < 0) {
            // The zero-length chunk already ended the body; a peer that hangs
            // up before the closing CRLF has still delivered all of it.
            state_ = State::kDone;
            return -1;
          }
          if (c == '\r') {
            break;
          }
          if (c == '\n') {
            if (trailer_line_len_ == 0) {
              state_ = State::kDone;
              return -1;
            }
            trailer_line_len_ = 0;  // end of one trailer field
            break;
          }
          ++trailer_line_len_;
          break;
        }

        case State::kBody:
        case State::kDone:
        case State::kError:
          return -1;
      }
    }
  }

  void startSizeLine() {
    chunk_remaining_ = 0;
    size_digits_ = 0;
    state_ = State::kSize;
  }

  Source* source_;
  BodyFraming framing_;
  int remaining_;  // identity: bytes left per Content-Length, < 0 if unknown
  State state_;
  uint32_t chunk_remaining_ = 0;
  int size_digits_ = 0;
  size_t trailer_line_len_ = 0;
  size_t total_ = 0;
  bool framing_error_ = false;
  bool truncated_ = false;
};

}  // namespace services::http
