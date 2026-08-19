#include "platform/png_decode.h"

#include <cstring>

namespace platform_png {

namespace {

constexpr uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

/** Terrain tiles are 256 px; the window is what makes anything wider pointless. */
constexpr int kMaxWidth = 256;
constexpr int kBytesPerPixel = 3;  // 8-bit truecolour, no alpha
constexpr int kMaxRowBytes = kMaxWidth * kBytesPerPixel;

constexpr size_t kWindowSize = 32768;  // RFC 1951 fixes this
constexpr size_t kWindowMask = kWindowSize - 1;

/** RFC 1950's Adler-32: modulus, and the run length zlib proves cannot overflow. */
constexpr uint32_t kAdlerBase = 65521;
constexpr uint32_t kAdlerRunMax = 5552;

/** Symbol counts of the three alphabets in RFC 1951, sections 3.2.5 - 3.2.7. */
constexpr int kLitSymbols = 288;
constexpr int kDistSymbols = 32;
constexpr int kCodeLenSymbols = 19;
constexpr int kMaxBits = 15;

/**
 * Canonical Huffman decode table: `count[n]` codes of length n, with the symbols
 * they map to listed in `symbol` in code order. Decoding walks the lengths and
 * needs no tree, which is what keeps this small enough to live in borrowed
 * memory (RFC 1951, section 3.2.2 spells out the construction).
 */
struct Huffman {
  int16_t count[kMaxBits + 1];
  int16_t symbol[kLitSymbols];
};

/**
 * Everything the decoder needs, laid over the caller's scratch in one piece.
 * Sized against kScratchBytes by the static_assert below, so the header's
 * promise to callers is checked at compile time rather than trusted.
 */
struct Work {
  Huffman lit;
  Huffman dist;
  uint8_t row_prev[kMaxRowBytes];
  uint8_t row_cur[kMaxRowBytes];
  uint8_t window[kWindowSize];
};

static_assert(sizeof(Work) <= kScratchBytes,
              "kScratchBytes no longer covers the decoder's working set");

ScratchFn s_scratch_fn = nullptr;

/** Length codes 257..285: base length and extra bits (RFC 1951, 3.2.5). */
constexpr uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                   15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                   67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

/** Distance codes 0..29: base distance and extra bits. */
constexpr uint16_t kDistBase[30] = {
    1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
    33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
    1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,
                                    4, 4, 5, 5, 6, 6, 7,  7,  8,  8,
                                    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/** Order the code-length alphabet's own lengths arrive in (RFC 1951, 3.2.7). */
constexpr uint8_t kCodeLenOrder[kCodeLenSymbols] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

int paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = p > a ? p - a : a - p;
  const int pb = p > b ? p - b : b - p;
  const int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) {
    return a;
  }
  return pb <= pc ? b : c;
}

/**
 * Reads the concatenated IDAT payloads as one byte stream, hiding the chunk
 * headers and CRCs from the inflate above it. Real tiles split their compressed
 * data across several IDATs at arbitrary points, so a boundary can fall
 * anywhere — mid-scanline, mid-Huffman-code.
 */
class IdatReader {
 public:
  explicit IdatReader(core::platform::BodyReader& body) : body_(body) {}

  /** -1 once the last IDAT has been consumed or the body ran out. */
  int readByte() {
    while (chunk_remaining_ == 0) {
      if (done_ || !nextIdatChunk()) {
        return -1;
      }
    }
    const int byte = body_.read();
    if (byte < 0) {
      done_ = true;
      return -1;
    }
    --chunk_remaining_;
    return byte;
  }

  /**
   * Consumes the header chunks up to the first IDAT and reports the image.
   * Returns false for a PNG this decoder does not handle, rather than guessing.
   */
  bool readHeader(int* width, int* height) {
    uint8_t signature[sizeof(kSignature)];
    if (!readExactly(signature, sizeof(signature)) ||
        memcmp(signature, kSignature, sizeof(kSignature)) != 0) {
      core::platform::logf("png: not a PNG\n");
      return false;
    }

    for (;;) {
      uint32_t length = 0;
      uint8_t type[4];
      if (!readUint32(&length) || !readExactly(type, sizeof(type))) {
        return false;
      }

      if (memcmp(type, "IHDR", 4) == 0) {
        uint8_t ihdr[13];
        if (length != sizeof(ihdr) || !readExactly(ihdr, sizeof(ihdr))) {
          return false;
        }
        width_ = static_cast<int>((static_cast<uint32_t>(ihdr[0]) << 24) |
                                  (static_cast<uint32_t>(ihdr[1]) << 16) |
                                  (static_cast<uint32_t>(ihdr[2]) << 8) |
                                  ihdr[3]);
        height_ = static_cast<int>((static_cast<uint32_t>(ihdr[4]) << 24) |
                                   (static_cast<uint32_t>(ihdr[5]) << 16) |
                                   (static_cast<uint32_t>(ihdr[6]) << 8) |
                                   ihdr[7]);
        const int bit_depth = ihdr[8];
        const int color_type = ihdr[9];
        const int interlace = ihdr[12];
        if (bit_depth != 8 || color_type != 2 || interlace != 0 ||
            width_ <= 0 || height_ <= 0 || width_ > kMaxWidth) {
          core::platform::logf(
              "png: unsupported %dx%d depth %d colour %d interlace %d\n", width_,
              height_, bit_depth, color_type, interlace);
          return false;
        }
        if (!skip(4)) {  // CRC
          return false;
        }
        continue;
      }

      if (memcmp(type, "IDAT", 4) == 0) {
        if (width_ == 0) {
          core::platform::logf("png: IDAT before IHDR\n");
          return false;
        }
        chunk_remaining_ = length;
        *width = width_;
        *height = height_;
        return true;
      }

      if (memcmp(type, "IEND", 4) == 0) {
        core::platform::logf("png: no image data\n");
        return false;
      }

      if (!skip(length + 4)) {  // ancillary chunk and its CRC
        return false;
      }
    }
  }

 private:
  /** Steps over the CRC of the IDAT just finished and onto the next one. */
  bool nextIdatChunk() {
    if (!skip(4)) {  // CRC of the chunk just consumed
      done_ = true;
      return false;
    }
    uint32_t length = 0;
    uint8_t type[4];
    if (!readUint32(&length) || !readExactly(type, sizeof(type))) {
      done_ = true;
      return false;
    }
    if (memcmp(type, "IDAT", 4) == 0) {
      chunk_remaining_ = length;
      return true;
    }
    // Anything else means the image data is over: IEND, or a trailing ancillary
    // chunk. Either way the inflate has all it is going to get.
    done_ = true;
    return false;
  }

  bool readExactly(uint8_t* buf, size_t len) {
    return body_.readBytes(reinterpret_cast<char*>(buf), len) == len;
  }

  bool readUint32(uint32_t* out) {
    uint8_t buf[4];
    if (!readExactly(buf, sizeof(buf))) {
      return false;
    }
    *out = (static_cast<uint32_t>(buf[0]) << 24) |
           (static_cast<uint32_t>(buf[1]) << 16) |
           (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
    return true;
  }

  bool skip(uint32_t count) {
    char sink[64];
    while (count > 0) {
      const size_t want = count < sizeof(sink) ? count : sizeof(sink);
      const size_t got = body_.readBytes(sink, want);
      if (got == 0) {
        return false;
      }
      count -= static_cast<uint32_t>(got);
    }
    return true;
  }

  core::platform::BodyReader& body_;
  uint32_t chunk_remaining_ = 0;
  bool done_ = false;
  int width_ = 0;
  int height_ = 0;
};

/**
 * The decode itself: an inflate whose output is consumed a byte at a time by the
 * PNG unfilter, so no raster and no decompressed copy is ever held. Both stages
 * share one Work in the caller's scratch.
 */
class Decoder {
 public:
  Decoder(Work* work, IdatReader& idat, int width, int height,
          core::terrain::PixelFn on_pixel, void* ctx)
      : w_(work),
        idat_(idat),
        width_(width),
        height_(height),
        row_bytes_(width * kBytesPerPixel),
        on_pixel_(on_pixel),
        ctx_(ctx) {
    memset(w_->row_prev, 0, sizeof(w_->row_prev));
  }

  bool run() {
    if (!readZlibHeader()) {
      return false;
    }

    bool final_block = false;
    while (!final_block && rows_done_ < height_) {
      final_block = takeBits(1) != 0;
      const uint32_t type = takeBits(2);
      if (failed_) {
        return false;
      }
      switch (type) {
        case 0:
          if (!storedBlock()) {
            return false;
          }
          break;
        case 1:
          buildFixedTables();
          if (!huffmanBlock()) {
            return false;
          }
          break;
        case 2:
          if (!buildDynamicTables() || !huffmanBlock()) {
            return false;
          }
          break;
        default:
          core::platform::logf("png: bad deflate block type\n");
          return false;
      }
    }

    if (rows_done_ != height_) {
      core::platform::logf("png: truncated after %d of %d rows\n", rows_done_,
                           height_);
      return false;
    }
    if (!final_block) {
      // The last scanline landed before the final block: whatever follows is
      // more DEFLATE, not the trailer, so there is nothing to compare against
      // yet. Every row is decoded, so this is a pass, just an unverified one.
      return true;
    }
    return verifyAdler();
  }

 private:
  // --- Bit reader ------------------------------------------------------------

  uint32_t takeBits(int count) {
    while (bit_count_ < count) {
      const int byte = idat_.readByte();
      if (byte < 0) {
        failed_ = true;
        return 0;
      }
      bits_ |= static_cast<uint32_t>(byte) << bit_count_;
      bit_count_ += 8;
    }
    const uint32_t value = bits_ & ((1u << count) - 1);
    bits_ >>= count;
    bit_count_ -= count;
    return value;
  }

  bool readZlibHeader() {
    const uint32_t cmf = takeBits(8);
    const uint32_t flg = takeBits(8);
    if (failed_) {
      return false;
    }
    if ((cmf & 0x0F) != 8 || ((cmf << 8) | flg) % 31 != 0 ||
        (flg & 0x20) != 0) {  // preset dictionary: never used by PNG
      core::platform::logf("png: bad zlib header\n");
      return false;
    }
    return true;
  }

  // --- Huffman ---------------------------------------------------------------

  /** Fills a table from per-symbol code lengths; see Huffman's note. */
  static void buildTable(Huffman* table, const uint8_t* lengths, int count) {
    memset(table->count, 0, sizeof(table->count));
    for (int i = 0; i < count; ++i) {
      ++table->count[lengths[i]];
    }
    table->count[0] = 0;

    int16_t offsets[kMaxBits + 2];
    offsets[1] = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
      offsets[len + 1] = static_cast<int16_t>(offsets[len] + table->count[len]);
    }
    for (int i = 0; i < count; ++i) {
      if (lengths[i] != 0) {
        table->symbol[offsets[lengths[i]]++] = static_cast<int16_t>(i);
      }
    }
  }

  /** -1 on a code that is not in the table, which means a corrupt stream. */
  int decodeSymbol(const Huffman& table) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
      code |= static_cast<int>(takeBits(1));
      if (failed_) {
        return -1;
      }
      const int count = table.count[len];
      if (code - first < count) {
        return table.symbol[index + (code - first)];
      }
      index += count;
      first = (first + count) << 1;
      code <<= 1;
    }
    return -1;
  }

  void buildFixedTables() {
    uint8_t lengths[kLitSymbols];
    for (int i = 0; i < 144; ++i) {
      lengths[i] = 8;
    }
    for (int i = 144; i < 256; ++i) {
      lengths[i] = 9;
    }
    for (int i = 256; i < 280; ++i) {
      lengths[i] = 7;
    }
    for (int i = 280; i < kLitSymbols; ++i) {
      lengths[i] = 8;
    }
    buildTable(&w_->lit, lengths, kLitSymbols);

    uint8_t dist_lengths[kDistSymbols];
    for (int i = 0; i < kDistSymbols; ++i) {
      dist_lengths[i] = 5;
    }
    buildTable(&w_->dist, dist_lengths, kDistSymbols);
  }

  bool buildDynamicTables() {
    const int lit_count = static_cast<int>(takeBits(5)) + 257;
    const int dist_count = static_cast<int>(takeBits(5)) + 1;
    const int code_len_count = static_cast<int>(takeBits(4)) + 4;
    if (failed_ || lit_count > kLitSymbols || dist_count > kDistSymbols) {
      core::platform::logf("png: bad dynamic table sizes\n");
      return false;
    }

    uint8_t code_len_lengths[kCodeLenSymbols] = {};
    for (int i = 0; i < code_len_count; ++i) {
      code_len_lengths[kCodeLenOrder[i]] = static_cast<uint8_t>(takeBits(3));
    }
    if (failed_) {
      return false;
    }
    // The code-length alphabet's table is transient, so it borrows the literal
    // table's storage: the real literal lengths are only decoded after it.
    Huffman& code_len_table = w_->lit;
    buildTable(&code_len_table, code_len_lengths, kCodeLenSymbols);

    uint8_t lengths[kLitSymbols + kDistSymbols] = {};
    int filled = 0;
    while (filled < lit_count + dist_count) {
      const int symbol = decodeSymbol(code_len_table);
      if (symbol < 0) {
        core::platform::logf("png: bad code length symbol\n");
        return false;
      }
      if (symbol < 16) {
        lengths[filled++] = static_cast<uint8_t>(symbol);
        continue;
      }

      int repeat = 0;
      uint8_t value = 0;
      if (symbol == 16) {
        if (filled == 0) {
          return false;  // nothing to repeat
        }
        value = lengths[filled - 1];
        repeat = 3 + static_cast<int>(takeBits(2));
      } else if (symbol == 17) {
        repeat = 3 + static_cast<int>(takeBits(3));
      } else {
        repeat = 11 + static_cast<int>(takeBits(7));
      }
      if (failed_ || filled + repeat > lit_count + dist_count) {
        return false;
      }
      while (repeat-- > 0) {
        lengths[filled++] = value;
      }
    }

    buildTable(&w_->lit, lengths, lit_count);
    buildTable(&w_->dist, lengths + lit_count, dist_count);
    return true;
  }

  // --- Blocks ----------------------------------------------------------------

  bool storedBlock() {
    // A stored block's length starts on a byte boundary, so only the partial
    // bits are dropped: whole buffered bytes are still part of the stream.
    const int partial = bit_count_ & 7;
    bits_ >>= partial;
    bit_count_ -= partial;

    const uint32_t len = takeBits(16);
    const uint32_t nlen = takeBits(16);
    if (failed_ || (len ^ 0xFFFF) != nlen) {
      core::platform::logf("png: bad stored block\n");
      return false;
    }
    for (uint32_t i = 0; i < len; ++i) {
      const int byte = idat_.readByte();
      if (byte < 0) {
        return false;
      }
      if (!emit(static_cast<uint8_t>(byte))) {
        return false;
      }
    }
    return true;
  }

  bool huffmanBlock() {
    for (;;) {
      const int symbol = decodeSymbol(w_->lit);
      if (symbol < 0) {
        core::platform::logf("png: bad literal code\n");
        return false;
      }
      if (symbol == 256) {
        return true;
      }
      if (symbol < 256) {
        if (!emit(static_cast<uint8_t>(symbol))) {
          return false;
        }
        continue;
      }

      const int length_index = symbol - 257;
      if (length_index >= 29) {
        core::platform::logf("png: bad length code\n");
        return false;
      }
      const int length =
          kLenBase[length_index] +
          static_cast<int>(takeBits(kLenExtra[length_index]));

      const int dist_symbol = decodeSymbol(w_->dist);
      if (dist_symbol < 0 || dist_symbol >= 30) {
        core::platform::logf("png: bad distance code\n");
        return false;
      }
      const uint32_t distance =
          kDistBase[dist_symbol] + takeBits(kDistExtra[dist_symbol]);
      if (failed_ || distance > written_) {
        core::platform::logf("png: distance runs off the window\n");
        return false;
      }

      for (int i = 0; i < length; ++i) {
        const uint8_t byte = w_->window[(written_ - distance) & kWindowMask];
        if (!emit(byte)) {
          return false;
        }
      }
    }
  }

  // --- Output: window, then unfilter, then pixels -----------------------------

  /**
   * One inflated byte. It goes into the window because a later back-reference
   * may need it, and into the scanline being assembled. `written_` counts the
   * whole stream, so masking it gives the circular position.
   */
  bool emit(uint8_t byte) {
    w_->window[written_ & kWindowMask] = byte;
    ++written_;
    updateAdler(byte);

    if (row_filter_ < 0) {
      row_filter_ = byte;  // every scanline is preceded by its filter type
      if (row_filter_ > 4) {
        core::platform::logf("png: bad row filter %d\n", row_filter_);
        return false;
      }
      return true;
    }

    w_->row_cur[row_filled_++] = byte;
    if (row_filled_ < row_bytes_) {
      return true;
    }

    unfilterRow();
    emitRow();

    // The row just finished is the next row's "above" neighbour.
    memcpy(w_->row_prev, w_->row_cur, static_cast<size_t>(row_bytes_));
    row_filled_ = 0;
    row_filter_ = -1;
    ++rows_done_;
    return true;
  }

  // --- Adler-32 (RFC 1950, section 9) ----------------------------------------

  /**
   * The sums are reduced every kAdlerRunMax bytes rather than every byte: that
   * is the longest run zlib proves cannot overflow 32 bits, and it turns a
   * division per inflated byte into one per 5552.
   */
  void updateAdler(uint8_t byte) {
    adler_a_ += byte;
    adler_b_ += adler_a_;
    if (++adler_run_ == kAdlerRunMax) {
      adler_a_ %= kAdlerBase;
      adler_b_ %= kAdlerBase;
      adler_run_ = 0;
    }
  }

  /**
   * Compares the stream's trailing checksum with the one we accumulated. This
   * is the only end-to-end check that the bytes we handed the caller are the
   * bytes the encoder compressed: everything else here validates structure, and
   * a stream can be structurally perfect while inflating to the wrong data.
   * Without it a corrupt tile becomes terrain that looks entirely plausible.
   */
  bool verifyAdler() {
    // The trailer is byte-aligned, but only the partial bits may be dropped —
    // whole bytes already pulled into the buffer are part of it.
    const int partial = bit_count_ % 8;
    bits_ >>= partial;
    bit_count_ -= partial;

    uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) {
      stored = (stored << 8) | takeBits(8);  // big-endian, unlike DEFLATE
    }
    if (failed_) {
      core::platform::logf("png: stream ends before its adler-32\n");
      return false;
    }

    adler_a_ %= kAdlerBase;
    adler_b_ %= kAdlerBase;
    const uint32_t computed = (adler_b_ << 16) | adler_a_;
    if (computed != stored) {
      core::platform::logf("png: adler-32 mismatch\n");
      return false;
    }
    return true;
  }

  /** PNG spec, section 9.2: reverses the filter in place using row_prev. */
  void unfilterRow() {
    uint8_t* cur = w_->row_cur;
    const uint8_t* prev = w_->row_prev;
    for (int i = 0; i < row_bytes_; ++i) {
      const int a = i >= kBytesPerPixel ? cur[i - kBytesPerPixel] : 0;
      const int b = prev[i];
      const int c = i >= kBytesPerPixel ? prev[i - kBytesPerPixel] : 0;
      int value = cur[i];
      switch (row_filter_) {
        case 1:
          value += a;
          break;
        case 2:
          value += b;
          break;
        case 3:
          value += (a + b) / 2;
          break;
        case 4:
          value += paeth(a, b, c);
          break;
        default:
          break;  // 0: stored as-is
      }
      cur[i] = static_cast<uint8_t>(value);
    }
  }

  void emitRow() {
    const uint8_t* row = w_->row_cur;
    const uint32_t y = static_cast<uint32_t>(rows_done_);
    for (int x = 0; x < width_; ++x) {
      const int i = x * kBytesPerPixel;
      on_pixel_(ctx_, static_cast<uint32_t>(x), y, row[i], row[i + 1],
                row[i + 2]);
    }
  }

  Work* w_;
  IdatReader& idat_;
  const int width_;
  const int height_;
  const int row_bytes_;
  core::terrain::PixelFn on_pixel_;
  void* ctx_;

  uint32_t bits_ = 0;
  int bit_count_ = 0;
  bool failed_ = false;

  uint32_t written_ = 0;  ///< bytes inflated, i.e. the circular window cursor
  int row_filter_ = -1;   ///< -1 while waiting for the next filter byte
  int row_filled_ = 0;
  int rows_done_ = 0;

  uint32_t adler_a_ = 1;  ///< RFC 1950 seeds the low half with 1, the high with 0
  uint32_t adler_b_ = 0;
  uint32_t adler_run_ = 0;
};

}  // namespace

void setScratch(ScratchFn fn) { s_scratch_fn = fn; }

bool decode(core::platform::BodyReader& body, core::terrain::PixelFn on_pixel,
            void* ctx) {
  if (s_scratch_fn == nullptr || on_pixel == nullptr) {
    return false;
  }
  uint8_t* scratch = s_scratch_fn(kScratchBytes);
  if (scratch == nullptr) {
    // The frame sprite is the lender, so this means it could not be created —
    // the radar is already painting straight to the panel and terrain is the
    // least of the problems.
    core::platform::logf("png: no scratch available\n");
    return false;
  }

  IdatReader idat(body);
  int width = 0;
  int height = 0;
  if (!idat.readHeader(&width, &height)) {
    return false;
  }

  Decoder decoder(reinterpret_cast<Work*>(scratch), idat, width, height,
                  on_pixel, ctx);
  return decoder.run();
}

}  // namespace platform_png
