#!/usr/bin/env python3
"""Build the PNG fixtures for test/test_png.

Regenerate with:

    python3 scripts/gen_png_fixtures.py

Every fixture is emitted together with the exact RGB raster it must decode to,
so the suite asserts pixel equality rather than "it did not crash". The PNGs are
assembled byte by byte here — chunk layout, per-row filter type and DEFLATE
block type are all chosen deliberately, which no PNG library would let us do.

Before anything is written, each fixture is decoded again by the reference
implementation at the bottom of this file (zlib plus our own unfilter) and
checked against the raster we are about to emit. A wrong fixture would send the
decoder author chasing a bug that does not exist, so that self-check is the
point of this script as much as the generation is.

Standard library only, and deterministic: rerunning it on an unchanged script
produces a byte-identical header.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_H = ROOT / "test" / "test_png" / "png_fixtures.h"

SIGNATURE = b"\x89PNG\r\n\x1a\n"

COLOUR_TYPE_GREY = 0
COLOUR_TYPE_RGB = 2

# Bytes per pixel per colour type — the filters' "left" offset.
BYTES_PER_PIXEL = {COLOUR_TYPE_GREY: 1, COLOUR_TYPE_RGB: 3}

# FNV-1a/64, used for the 256x256 tile whose raster is far too large to emit as
# a C array. The test recomputes it over the RGB bytes in callback order, so it
# is an exact check on the values AND their order, not a spot check.
FNV_OFFSET_BASIS = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
FNV_MASK = 0xFFFFFFFFFFFFFFFF

BYTES_PER_LINE = 12


# --- PNG assembly -------------------------------------------------------------


def chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def ihdr(
    width: int,
    height: int,
    colour_type: int = COLOUR_TYPE_RGB,
    interlace: int = 0,
) -> bytes:
    return chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, interlace),
    )


def paeth_predictor(left: int, up: int, up_left: int) -> int:
    estimate = left + up - up_left
    d_left = abs(estimate - left)
    d_up = abs(estimate - up)
    d_up_left = abs(estimate - up_left)
    if d_left <= d_up and d_left <= d_up_left:
        return left
    if d_up <= d_up_left:
        return up
    return up_left


def predictor(
    filter_type: int, index: int, raw: bytes, prior: bytes, bpp: int
) -> int:
    """RFC 2083 predictor for byte `index`, from already-unfiltered bytes."""
    left = raw[index - bpp] if index >= bpp else 0
    up = prior[index]
    up_left = prior[index - bpp] if index >= bpp else 0
    if filter_type == 0:
        return 0
    if filter_type == 1:
        return left
    if filter_type == 2:
        return up
    if filter_type == 3:
        return (left + up) // 2
    if filter_type == 4:
        return paeth_predictor(left, up, up_left)
    raise ValueError(f"unknown filter type {filter_type}")


def filter_row(filter_type: int, raw: bytes, prior: bytes, bpp: int) -> bytes:
    out = bytearray(len(raw))
    for index, value in enumerate(raw):
        pred = predictor(filter_type, index, raw, prior, bpp)
        out[index] = (value - pred) & 0xFF
    return bytes(out)


def unfilter_row(
    filter_type: int, filtered: bytes, prior: bytes, bpp: int
) -> bytes:
    out = bytearray(len(filtered))
    for index, value in enumerate(filtered):
        pred = predictor(filter_type, index, out, prior, bpp)
        out[index] = (value + pred) & 0xFF
    return bytes(out)


def filtered_stream(
    width: int,
    height: int,
    samples: bytes,
    filters: tuple[int, ...],
    bpp: int,
) -> bytes:
    """The IDAT payload before compression: filter byte plus row, per row."""
    stride = width * bpp
    prior = bytes(stride)
    out = bytearray()
    for row in range(height):
        raw = samples[row * stride : (row + 1) * stride]
        filter_type = filters[row % len(filters)]
        out.append(filter_type)
        out += filter_row(filter_type, raw, prior, bpp)
        prior = raw
    return bytes(out)


def deflate(data: bytes, level: int = -1, strategy: int | None = None) -> bytes:
    if strategy is None:
        return zlib.compress(data, level)
    compressor = zlib.compressobj(level, zlib.DEFLATED, zlib.MAX_WBITS, 8, strategy)
    return compressor.compress(data) + compressor.flush()


def first_block_type(stream: bytes) -> int:
    """BTYPE of the first DEFLATE block: 0 stored, 1 fixed, 2 dynamic.

    Bits run least-significant-first inside each byte, so the block header sits
    in the low three bits of the byte after zlib's two-byte wrapper: bit 0 is
    BFINAL, bits 1-2 are BTYPE.
    """
    return (stream[2] >> 1) & 0x3


def deflate_fixed_huffman(data: bytes) -> bytes:
    """A stream whose first block is fixed-Huffman (BTYPE 1).

    Z_FIXED asks zlib for exactly that. Falling back to a search over levels
    matters because whether a given input lands on fixed or dynamic trees is a
    size heuristic inside zlib, not something the API promises.
    """
    candidates: list[tuple[int, int | None]] = []
    if hasattr(zlib, "Z_FIXED"):
        candidates.append((9, zlib.Z_FIXED))
    candidates += [(level, None) for level in range(1, 10)]
    for level, strategy in candidates:
        stream = deflate(data, level, strategy)
        if first_block_type(stream) == 1:
            return stream
    raise AssertionError("no zlib setting produced a fixed-Huffman first block")


# --- DEFLATE stream analysis --------------------------------------------------
#
# zlib will decompress a stream but will not say where its blocks are, and one
# fixture property depends on exactly that: png_decode.cpp stops reading as soon
# as the last scanline is complete, and verifies the Adler-32 trailer only if it
# has seen the BFINAL flag by then. A stream whose raster finishes inside a
# non-final block therefore never reaches the check, however wrong its trailer
# is — which would make an Adler fixture silently prove nothing. So the blocks
# are walked here, with the walk's own output checked against zlib's.

LEN_BASE = (
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258,
)
LEN_EXTRA = (
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
    5, 5, 5, 5, 0,
)
DIST_BASE = (
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
)
DIST_EXTRA = (
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
    11, 11, 12, 12, 13, 13,
)
CODE_LEN_ORDER = (16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15)


class BitReader:
    """LSB-first bit reader, the order RFC 1951 packs codes in."""

    def __init__(self, data: bytes, pos: int = 0) -> None:
        self.data = data
        self.pos = pos
        self.bit = 0

    def bits(self, count: int) -> int:
        value = 0
        for index in range(count):
            if self.pos >= len(self.data):
                raise ValueError("deflate stream ends mid-block")
            value |= ((self.data[self.pos] >> self.bit) & 1) << index
            self.bit += 1
            if self.bit == 8:
                self.bit = 0
                self.pos += 1
        return value

    def align(self) -> None:
        if self.bit != 0:
            self.bit = 0
            self.pos += 1


def huffman_table(lengths: list[int]) -> tuple[list[int], list[int]]:
    counts = [0] * 16
    for length in lengths:
        counts[length] += 1
    counts[0] = 0
    offsets = [0] * 17
    for length in range(1, 16):
        offsets[length + 1] = offsets[length] + counts[length]
    symbols = [0] * sum(counts)
    for symbol, length in enumerate(lengths):
        if length != 0:
            symbols[offsets[length]] = symbol
            offsets[length] += 1
    return counts, symbols


def huffman_decode(reader: BitReader, table: tuple[list[int], list[int]]) -> int:
    counts, symbols = table
    code = 0
    first = 0
    index = 0
    for length in range(1, 16):
        code |= reader.bits(1)
        count = counts[length]
        if code - first < count:
            return symbols[index + code - first]
        index += count
        first = (first + count) << 1
        code <<= 1
    raise ValueError("code longer than 15 bits")


def fixed_tables() -> tuple[tuple[list[int], list[int]], tuple[list[int], list[int]]]:
    lit = [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8
    return huffman_table(lit), huffman_table([5] * 30)


def dynamic_tables(
    reader: BitReader,
) -> tuple[tuple[list[int], list[int]], tuple[list[int], list[int]]]:
    lit_count = reader.bits(5) + 257
    dist_count = reader.bits(5) + 1
    code_len_count = reader.bits(4) + 4

    code_len_lengths = [0] * 19
    for index in range(code_len_count):
        code_len_lengths[CODE_LEN_ORDER[index]] = reader.bits(3)
    code_len_table = huffman_table(code_len_lengths)

    lengths: list[int] = []
    while len(lengths) < lit_count + dist_count:
        symbol = huffman_decode(reader, code_len_table)
        if symbol < 16:
            lengths.append(symbol)
        elif symbol == 16:
            lengths += [lengths[-1]] * (3 + reader.bits(2))
        elif symbol == 17:
            lengths += [0] * (3 + reader.bits(3))
        else:
            lengths += [0] * (11 + reader.bits(7))
    return (
        huffman_table(lengths[:lit_count]),
        huffman_table(lengths[lit_count:lit_count + dist_count]),
    )


@dataclass
class DeflateBlock:
    final: bool
    btype: int
    output_end: int  # decompressed bytes produced by the end of this block


def deflate_blocks(stream: bytes) -> tuple[list[DeflateBlock], bytes, int]:
    """Walk a zlib stream: its blocks, its output, and where its trailer starts."""
    reader = BitReader(stream, 2)  # past the two-byte zlib wrapper
    out = bytearray()
    blocks: list[DeflateBlock] = []
    while True:
        final = reader.bits(1) == 1
        btype = reader.bits(2)
        if btype == 0:
            reader.align()
            length = reader.bits(16)
            nlength = reader.bits(16)
            assert length ^ 0xFFFF == nlength, "stored block LEN/NLEN disagree"
            for _ in range(length):
                out.append(reader.bits(8))
        elif btype in (1, 2):
            lit, dist = fixed_tables() if btype == 1 else dynamic_tables(reader)
            while True:
                symbol = huffman_decode(reader, lit)
                if symbol == 256:
                    break
                if symbol < 256:
                    out.append(symbol)
                    continue
                index = symbol - 257
                length = LEN_BASE[index] + reader.bits(LEN_EXTRA[index])
                dist_symbol = huffman_decode(reader, dist)
                distance = DIST_BASE[dist_symbol] + reader.bits(
                    DIST_EXTRA[dist_symbol]
                )
                for _ in range(length):
                    out.append(out[-distance])
        else:
            raise ValueError("reserved block type")
        blocks.append(DeflateBlock(final, btype, len(out)))
        if final:
            break
    reader.align()
    return blocks, bytes(out), reader.pos


def completing_block(blocks: list[DeflateBlock], raw_len: int) -> DeflateBlock:
    """The block png_decode.cpp is inside when the last scanline byte lands.

    Its BFINAL flag is what decides whether the Adler-32 check runs at all: the
    decoder's loop ends on the first block that either carries BFINAL or takes
    the row count to the image height, and only verifies in the former case.
    """
    for block in blocks:
        if block.final or block.output_end >= raw_len:
            return block
    raise AssertionError("stream ends before the raster does")


def raster_completing_block(stream: bytes, raw_len: int) -> DeflateBlock:
    """As above, for a healthy stream, cross-checked against zlib's own output."""
    blocks, out, _ = deflate_blocks(stream)
    assert out == zlib.decompress(stream), "block walk disagrees with zlib"
    return completing_block(blocks, raw_len)


def reaches_adler_check(stream: bytes, raw_len: int) -> bool:
    """Whether a DAMAGED stream still gets as far as the trailer comparison.

    Same question as above, minus the cross-check against zlib: these streams are
    ones zlib refuses outright, which is the reason they exist.
    """
    blocks, _, _ = deflate_blocks(stream)
    return completing_block(blocks, raw_len).final


def split_idat(stream: bytes, boundaries: tuple[int, ...]) -> bytes:
    """The compressed stream carved into consecutive IDAT chunks."""
    cuts = [0, *boundaries, len(stream)]
    parts = [stream[a:b] for a, b in zip(cuts, cuts[1:])]
    return b"".join(chunk(b"IDAT", part) for part in parts)


def make_png(
    width: int,
    height: int,
    samples: bytes,
    *,
    filters: tuple[int, ...] = (0,),
    colour_type: int = COLOUR_TYPE_RGB,
    interlace: int = 0,
    level: int = -1,
    strategy: int | None = None,
    fixed_huffman: bool = False,
    stream: bytes | None = None,
    idat_boundaries: tuple[int, ...] = (),
    before_idat: bytes = b"",
    after_idat: bytes = b"",
) -> bytes:
    bpp = BYTES_PER_PIXEL[colour_type]
    raw = filtered_stream(width, height, samples, filters, bpp)
    if stream is None:
        if fixed_huffman:
            stream = deflate_fixed_huffman(raw)
        else:
            stream = deflate(raw, level, strategy)
    return b"".join(
        [
            SIGNATURE,
            ihdr(width, height, colour_type, interlace),
            before_idat,
            split_idat(stream, idat_boundaries),
            after_idat,
            chunk(b"IEND", b""),
        ]
    )


# --- Ancillary chunks the decoder has to walk past ----------------------------


def phys_chunk() -> bytes:
    """96 dpi in both axes, metre units — what an image editor writes."""
    return chunk(b"pHYs", struct.pack(">IIB", 3780, 3780, 1))


def text_chunk(keyword: str, value: str) -> bytes:
    return chunk(b"tEXt", keyword.encode("latin-1") + b"\x00" + value.encode("latin-1"))


# --- Pixel patterns ----------------------------------------------------------


def wrapping_pattern(width: int, height: int, seed: int = 0) -> bytes:
    """A pattern whose channels run past 0 and 255 between neighbours.

    Wraparound is where filter arithmetic goes wrong: every predictor is
    defined modulo 256, so a decoder that subtracts into a signed intermediate
    or clamps instead of wrapping decodes a smooth gradient perfectly and this
    not at all.
    """
    out = bytearray()
    for y in range(height):
        for x in range(width):
            out += bytes(
                (
                    (x * 37 + y * 11 + seed) & 0xFF,
                    (255 - x * 29 + y * 61 + seed) & 0xFF,
                    (x * x + y * y * 7 + seed * 3) & 0xFF,
                )
            )
    return bytes(out)


def flat_pattern(width: int, height: int) -> bytes:
    """Two colours in long runs, i.e. an input DEFLATE compresses to nothing."""
    out = bytearray()
    for y in range(height):
        for x in range(width):
            out += b"\x20\x40\x60" if (x + y) % 8 < 4 else b"\x20\x40\x61"
    return bytes(out)


def mottled_pattern(width: int, height: int, seed: int = 1) -> bytes:
    """A ramp with low-amplitude noise on it, from a local LCG.

    Tuned to make zlib build dynamic Huffman trees, which flat-out noise does
    not: incompressible input is emitted as STORED blocks, so the obvious
    "large noisy image" gives the same code path as the level-0 fixture. What
    dynamic trees need is a skewed but varied literal distribution, i.e. this.
    """
    out = bytearray()
    state = seed
    for y in range(height):
        for x in range(width):
            state = (state * 1103515245 + 12345) & 0x7FFFFFFF
            noise = (state >> 16) & 0x0F
            out += bytes(
                (
                    (x * 3 + noise) & 0xFF,
                    (y * 5 + noise) & 0xFF,
                    (0x40 + (noise << 1)) & 0xFF,
                )
            )
    return bytes(out)


def terrarium_pattern(size: int) -> bytes:
    """A terrarium-encoded tile: height_m = R*256 + G + B/256 - 32768.

    Elevation steps every 16 px on both axes and dips below sea level in the
    north-west corner, so the tile reads as recognisable terrain rather than
    noise while staying compressible enough to emit as a C array. B carries a
    sub-metre remainder, which a decoder that quietly drops the third channel
    would otherwise get away with.
    """
    out = bytearray()
    for y in range(size):
        for x in range(size):
            elevation = (x >> 4) * 137 + (y >> 4) * 211 - 400
            encoded = elevation + 32768
            out += bytes(
                (
                    (encoded >> 8) & 0xFF,
                    encoded & 0xFF,
                    ((x >> 4) * 16 + (y & 0x0F)) & 0xFF,
                )
            )
    return bytes(out)


# --- Fixtures ----------------------------------------------------------------


@dataclass
class Fixture:
    """One fixture: `name` is what a failing assertion in the suite prints."""

    name: str
    symbol: str
    doc: str
    png: bytes
    width: int
    height: int
    pixels: bytes | None
    emit_pixels: bool = True
    rejected: bool = False
    # BTYPE the first DEFLATE block must have, where that is the point of the
    # fixture. zlib chooses it by a size heuristic, so it is asserted, not hoped.
    block_type: int | None = None

    # Whether the block that completes the raster carries BFINAL, which is what
    # decides whether png_decode.cpp reaches its Adler-32 check at all. Asserted
    # for every fixture: a checksum fixture that quietly stopped reaching the
    # check would pass for the wrong reason, and a plain image that stopped
    # reaching it would silently drop the check from the suite's coverage.
    verifies_adler: bool = True

    # The filtered stream the encoder compressed, kept where a self-check has to
    # compare what a damaged stream now inflates to against what it should.
    expected_raw: bytes | None = None


FILTER_WIDTH = 9  # 27 bytes a row: not a multiple of 4, so no alignment luck
FILTER_HEIGHT = 5

FILTER_NAMES = {0: "none", 1: "sub", 2: "up", 3: "average", 4: "paeth"}


def build_fixtures() -> list[Fixture]:
    fixtures: list[Fixture] = []

    # One image, five encodings: the five per-filter fixtures share this raster,
    # so a failure in exactly one of them names the filter that is wrong.
    filter_pixels = wrapping_pattern(FILTER_WIDTH, FILTER_HEIGHT)
    for filter_type, filter_name in sorted(FILTER_NAMES.items()):
        fixtures.append(
            Fixture(
                name=f"filter_{filter_name}",
                symbol=f"Filter{filter_name.capitalize()}",
                doc=(
                    f"Filter {filter_type} ({filter_name.capitalize()}) on every "
                    "row of the shared wrapping-value raster."
                ),
                png=make_png(
                    FILTER_WIDTH,
                    FILTER_HEIGHT,
                    filter_pixels,
                    filters=(filter_type,),
                ),
                width=FILTER_WIDTH,
                height=FILTER_HEIGHT,
                pixels=filter_pixels,
            )
        )

    fixtures.append(
        Fixture(
            name="mixed_filters",
            symbol="MixedFilters",
            doc=(
                "The same raster with a different filter on each of the five "
                "rows, which is what a real encoder emits: it picks per row. A "
                "decoder that reads the filter byte once decodes row 0 and "
                "then diverges."
            ),
            png=make_png(
                FILTER_WIDTH,
                FILTER_HEIGHT,
                filter_pixels,
                filters=(4, 0, 3, 1, 2),
            ),
            width=FILTER_WIDTH,
            height=FILTER_HEIGHT,
            pixels=filter_pixels,
        )
    )

    stored_pixels = wrapping_pattern(16, 8, seed=5)
    fixtures.append(
        Fixture(
            name="stored_deflate",
            symbol="StoredDeflate",
            doc=(
                "Stored (uncompressed) DEFLATE blocks, level 0. No Huffman "
                "decoding happens at all here, so it isolates the block "
                "framing and the copy path."
            ),
            png=make_png(16, 8, stored_pixels, filters=(1,), level=0),
            width=16,
            height=8,
            pixels=stored_pixels,
            block_type=0,
        )
    )

    fixed_pixels = flat_pattern(8, 4)
    fixtures.append(
        Fixture(
            name="fixed_huffman",
            symbol="FixedHuffman",
            doc=(
                "Fixed-Huffman blocks: the literal and distance code lengths "
                "are the ones RFC 1951 hard-codes, so no tree is transmitted "
                "and the decoder must supply them itself."
            ),
            png=make_png(8, 4, fixed_pixels, fixed_huffman=True),
            width=8,
            height=4,
            pixels=fixed_pixels,
            block_type=1,
        )
    )

    dynamic_pixels = mottled_pattern(32, 16)
    fixtures.append(
        Fixture(
            name="dynamic_huffman",
            symbol="DynamicHuffman",
            doc=(
                "Dynamic-Huffman blocks: trees built from the code-length "
                "alphabet in the block header, including its run-length "
                "escapes. This is what real tiles are."
            ),
            png=make_png(32, 16, dynamic_pixels),
            width=32,
            height=16,
            pixels=dynamic_pixels,
            block_type=2,
        )
    )

    fixtures.append(build_split_idat_fixture())
    fixtures.append(build_mid_scanline_split_fixture())

    ancillary_pixels = wrapping_pattern(10, 4, seed=9)
    fixtures.append(
        Fixture(
            name="ancillary_chunks",
            symbol="AncillaryChunks",
            doc=(
                "pHYs and tEXt before the image data and another tEXt after "
                "it. All three must be walked past on chunk length alone; a "
                "decoder that only knows IHDR/IDAT/IEND stops at the first."
            ),
            png=make_png(
                10,
                4,
                ancillary_pixels,
                filters=(2,),
                before_idat=phys_chunk() + text_chunk("Software", "gen_png_fixtures"),
                after_idat=text_chunk("Comment", "trailing ancillary chunk"),
            ),
            width=10,
            height=4,
            pixels=ancillary_pixels,
        )
    )

    fixtures.append(build_empty_final_block_fixture())
    fixtures.append(build_terrarium_fixture())
    fixtures += build_reject_fixtures(dynamic_pixels)
    return fixtures


def build_empty_final_block_fixture() -> Fixture:
    """An encoder that finishes the raster and only then closes the stream.

    Z_SYNC_FLUSH ends the data block without BFINAL and appends an empty stored
    block; Z_FINISH then adds a final empty block and the trailer. So the last
    scanline lands inside a NON-final block, which is the one shape where
    png_decode.cpp deliberately returns without checking the Adler-32: at that
    point the trailer is not what comes next, so comparing would be wrong.

    This fixture is what keeps that concession from rotting. It must decode, and
    it is the one decodable fixture whose verifies_adler is false — if a later
    change to the checksum code starts refusing it, an encoder that flushes is
    an encoder whose tiles stop working.
    """
    width, height = 12, 6
    pixels = wrapping_pattern(width, height, seed=11)
    raw = filtered_stream(width, height, pixels, (1,), 3)

    compressor = zlib.compressobj()
    stream = compressor.compress(raw)
    stream += compressor.flush(zlib.Z_SYNC_FLUSH)
    stream += compressor.flush(zlib.Z_FINISH)

    return Fixture(
        name="empty_final_block",
        symbol="EmptyFinalBlock",
        doc=(
            "The raster ends inside a non-final block, with an empty BFINAL "
            "block after it — what a flushing encoder emits. Must decode; it "
            "is also the one image where the Adler-32 is deliberately not "
            "verified, because the trailer is not what follows the data."
        ),
        png=make_png(width, height, pixels, filters=(1,), stream=stream),
        width=width,
        height=height,
        pixels=pixels,
        verifies_adler=False,
    )


def build_split_idat_fixture() -> Fixture:
    """Compressed data spread over five IDAT chunks, one of them empty.

    Chunk boundaries in a real tile fall wherever the encoder's buffer ran out,
    which is to say nowhere in particular: not on a block boundary, not on a
    scanline. A zero-length IDAT is legal too (the spec constrains only that the
    chunks be consecutive) and is the boundary case most likely to be read as
    end of image.
    """
    width, height = 12, 6
    pixels = wrapping_pattern(width, height, seed=3)
    stream = deflate(filtered_stream(width, height, pixels, (4,), 3))
    quarter = len(stream) // 4
    boundaries = (quarter, quarter, 2 * quarter, 3 * quarter)
    return Fixture(
        name="split_idat",
        symbol="SplitIdat",
        doc=(
            "One DEFLATE stream carved across five IDAT chunks, including a "
            "zero-length one. Nothing may be assumed to align with a chunk."
        ),
        png=make_png(
            width,
            height,
            pixels,
            filters=(4,),
            idat_boundaries=boundaries,
        ),
        width=width,
        height=height,
        pixels=pixels,
    )


def build_mid_scanline_split_fixture() -> Fixture:
    """A chunk boundary provably inside a scanline.

    Where a compressed boundary lands in the decompressed stream is not ours to
    choose, so this fixture uses stored blocks, where it is: after zlib's
    two-byte wrapper and one five-byte block header the mapping is byte for
    byte. The cut is placed at one and a half rows, i.e. mid-scanline, with the
    arithmetic asserted below rather than assumed.
    """
    width, height = 12, 6
    pixels = wrapping_pattern(width, height, seed=7)
    raw = filtered_stream(width, height, pixels, (3,), 3)
    stream = deflate(raw, 0)
    assert first_block_type(stream) == 0, "level 0 did not produce stored blocks"

    row_bytes = 1 + width * 3
    raw_offset = row_bytes + row_bytes // 2
    stored_payload_offset = 2 + 5  # zlib wrapper, then the stored block header
    boundary = stored_payload_offset + raw_offset
    assert stream[boundary] == raw[raw_offset], "stored mapping is not byte for byte"
    assert raw_offset % row_bytes not in (0, 1), "cut is not mid-scanline"

    return Fixture(
        name="split_idat_mid_scanline",
        symbol="SplitIdatMidScanline",
        doc=(
            "Two IDAT chunks whose boundary falls in the middle of row 1, so "
            "the decoder has to carry half a scanline across a chunk."
        ),
        png=make_png(
            width,
            height,
            pixels,
            filters=(3,),
            level=0,
            idat_boundaries=(boundary,),
        ),
        width=width,
        height=height,
        pixels=pixels,
        block_type=0,
    )


def build_terrarium_fixture() -> Fixture:
    """The real thing: a 256x256 tile, the only size the firmware ever fetches.

    Its 768-byte scanlines and 65536 pixels are what exercise the two-scanline
    scratch and back-references reaching well beyond one row.
    """
    size = 256
    pixels = terrarium_pattern(size)
    return Fixture(
        name="terrarium_tile",
        symbol="TerrariumTile",
        doc=(
            "A full-size 256x256 terrarium tile with mixed per-row filters. "
            "Its raster is checked by hash and by first and last row, not as a "
            "196608-byte array."
        ),
        png=make_png(size, size, pixels, filters=(0, 1, 2, 3, 4)),
        width=size,
        height=size,
        pixels=pixels,
        emit_pixels=False,
    )


def build_reject_fixtures(dynamic_pixels: bytes) -> list[Fixture]:
    grey = bytes(((x * 31 + y * 17) & 0xFF for y in range(4) for x in range(8)))
    greyscale_png = make_png(
        8, 4, grey, filters=(1,), colour_type=COLOUR_TYPE_GREY
    )

    interlaced_pixels = wrapping_pattern(8, 8, seed=2)
    # Adam7 would reorder the raster, but the interlace flag has to be refused
    # from IHDR before any of it is read, so the payload is left progressive.
    interlaced_png = make_png(8, 8, interlaced_pixels, interlace=1)

    valid_png = make_png(32, 16, dynamic_pixels, filters=(4,))
    truncated_png = truncate_inside_idat(valid_png)
    corrupt_png = corrupt_deflate(valid_png)

    bad_signature_png = bytearray(valid_png)
    bad_signature_png[1] = ord("Q")

    # The three checksum cases share this base image, so what separates them is
    # only which part of a perfectly good stream was damaged.
    expected_raw = filtered_stream(32, 16, dynamic_pixels, (4,), 3)
    row_bytes = 1 + 32 * 3
    adler_rejects = [
        Fixture(
            name="reject_wrong_adler",
            symbol="RejectWrongAdler",
            doc=(
                "Last byte of the stream flipped, i.e. the Adler-32 trailer and "
                "nothing else. The image data inflates perfectly to the right "
                "raster, so this fixture fails only if the checksum is read and "
                "compared — it is the one that proves the check runs at all."
            ),
            png=corrupt_adler_trailer(valid_png),
            width=32,
            height=16,
            pixels=None,
            emit_pixels=False,
            rejected=True,
            expected_raw=expected_raw,
        ),
        Fixture(
            name="reject_silent_corruption",
            symbol="RejectSilentCorruption",
            doc=(
                "Image data altered so that it still inflates, to a full raster "
                "of the right size but the wrong bytes, against an untouched "
                "trailer. Every structural check in the decoder passes and a "
                "whole tile of plausible-looking terrain is handed over: the "
                "checksum is the only thing standing between that and the map."
            ),
            png=corrupt_silently(valid_png, expected_raw, row_bytes),
            width=32,
            height=16,
            pixels=None,
            emit_pixels=False,
            rejected=True,
            expected_raw=expected_raw,
        ),
        Fixture(
            name="reject_missing_adler",
            symbol="RejectMissingAdler",
            doc=(
                "The stream cut off exactly after the last scanline byte, so "
                "the raster is complete and the trailer is simply absent — the "
                "body ended four bytes early."
            ),
            png=drop_adler_trailer(valid_png),
            width=32,
            height=16,
            pixels=None,
            emit_pixels=False,
            rejected=True,
            expected_raw=expected_raw,
        ),
    ]

    return adler_rejects + [build_extra_scanline_fixture()] + [
        Fixture(
            name="reject_greyscale",
            symbol="RejectGreyscale",
            doc="Colour type 0. Rejected: only truecolour RGB is accepted.",
            png=greyscale_png,
            width=8,
            height=4,
            pixels=None,
            emit_pixels=False,
            rejected=True,
        ),
        Fixture(
            name="reject_interlaced",
            symbol="RejectInterlaced",
            doc=(
                "Adam7 interlace flag set. Rejected: streaming an interlaced "
                "image would need the whole raster in RAM."
            ),
            png=interlaced_png,
            width=8,
            height=8,
            pixels=None,
            emit_pixels=False,
            rejected=True,
        ),
        Fixture(
            name="reject_truncated",
            symbol="RejectTruncated",
            doc=(
                "A valid PNG cut short inside IDAT — a dropped connection. "
                "Rejected: the rows that did arrive are not an image."
            ),
            png=truncated_png,
            width=32,
            height=16,
            pixels=None,
            emit_pixels=False,
            rejected=True,
        ),
        Fixture(
            name="reject_corrupt_deflate",
            symbol="RejectCorruptDeflate",
            doc=(
                "Bytes flipped inside the compressed stream so that it no "
                "longer inflates, with the chunk CRC recomputed. The failure "
                "therefore has to come out of inflate itself: not out of the "
                "chunk CRC, and not out of zlib's Adler-32 trailer, which a "
                "decoder this size may reasonably not check."
            ),
            png=corrupt_png,
            width=32,
            height=16,
            pixels=None,
            emit_pixels=False,
            rejected=True,
        ),
        Fixture(
            name="reject_bad_signature",
            symbol="RejectBadSignature",
            doc="One byte of the eight-byte signature changed.",
            png=bytes(bad_signature_png),
            width=32,
            height=16,
            pixels=None,
            emit_pixels=False,
            rejected=True,
        ),
    ]


def build_extra_scanline_fixture() -> Fixture:
    """A stream that keeps going after the height in IHDR says it should stop.

    One extra filter byte and one extra scanline: the smallest overrun that can
    actually reach the pixel sink, because a shorter one never completes a row.
    Everything else about the image is impeccable — it inflates cleanly, its
    declared rows are correct, the extra row's filter byte is a legal 0, and the
    Adler-32 covers the over-long data and therefore matches. So the row count is
    the only thing left that can refuse it, which is what makes this fixture
    evidence about that guard and nothing else.

    Unlike the other rejects it emits its expected raster, because half of what
    it asserts is that the declared rows arrived intact before the refusal.
    """
    width, height = 32, 16
    pixels = mottled_pattern(width, height, seed=23)
    raw = filtered_stream(width, height, pixels, (2,), 3)
    extra = bytes([0]) + bytes([0x77] * (width * 3))

    return Fixture(
        name="reject_extra_scanline",
        symbol="RejectExtraScanline",
        doc=(
            "IHDR declares 16 rows; the image data inflates to 17. Rejected — "
            "and the seventeenth row must never reach the sink, which is "
            "entitled to the bounds PixelFn promises it."
        ),
        png=make_png(width, height, pixels, filters=(2,), stream=deflate(raw + extra)),
        width=width,
        height=height,
        pixels=pixels,
        rejected=True,
        expected_raw=raw,
    )


def find_chunk(data: bytes, tag: bytes) -> tuple[int, int]:
    """(payload offset, payload length) of the first `tag` chunk."""
    pos = len(SIGNATURE)
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        if data[pos + 4 : pos + 8] == tag:
            return pos + 8, length
        pos += 12 + length
    raise AssertionError(f"no {tag!r} chunk")


def idat_stream(data: bytes) -> bytes:
    """Every IDAT payload concatenated — the DEFLATE stream as a decoder sees it."""
    pos = len(SIGNATURE)
    out = bytearray()
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        if data[pos + 4 : pos + 8] == b"IDAT":
            out += data[pos + 8 : pos + 8 + length]
        pos += 12 + length
    return bytes(out)


def truncate_inside_idat(data: bytes) -> bytes:
    offset, length = find_chunk(data, b"IDAT")
    assert length > 8, "IDAT too short to cut inside"
    return data[: offset + length // 2]


def inflate_raw(stream: bytes) -> bytes | None:
    """The DEFLATE data alone, ignoring zlib's Adler-32 trailer; None if it fails.

    Takes a whole zlib stream and steps over its two-byte header, so what comes
    back is what an inflater sees before any checksum is consulted. That
    distinction is what tells the two corruption fixtures apart: one flips bytes
    until inflate itself chokes, the other until inflate is perfectly happy and
    only the checksum knows.
    """
    inflater = zlib.decompressobj(-zlib.MAX_WBITS)
    try:
        return inflater.decompress(stream[2:])
    except zlib.error:
        return None


def replace_idat(data: bytes, stream: bytes) -> bytes:
    """The same PNG with its image data swapped and the chunk CRC recomputed."""
    offset, length = find_chunk(data, b"IDAT")
    assert idat_stream(data) == data[offset : offset + length], "expected one IDAT"
    return data[: offset - 8] + chunk(b"IDAT", stream) + data[offset + length + 4 :]


def corrupt_deflate(data: bytes) -> bytes:
    """Flip bytes inside the compressed stream until it no longer inflates.

    Late in the payload by preference, so some rows have already been handed to
    the pixel sink by the time the stream turns to nonsense: giving up cleanly
    part way through an image is harder than refusing one at the header.
    """
    offset, length = find_chunk(data, b"IDAT")
    order = list(range(length // 2, length - 1)) + list(range(2, length // 2))
    for shift in order:
        payload = bytearray(data[offset : offset + length])
        payload[shift] ^= 0xFF
        payload[shift + 1] ^= 0x5A
        if inflate_raw(bytes(payload)) is not None:
            continue
        return replace_idat(data, bytes(payload))
    raise AssertionError("no byte flip in IDAT made the stream uninflatable")


def corrupt_silently(data: bytes, expected_raw: bytes, row_bytes: int) -> bytes:
    """Alter the image data so it still inflates, to the wrong bytes.

    The corruption nothing but the checksum can catch, and the reason a decoder
    wants one: the stream is structurally perfect, inflates without complaint,
    and produces a full raster of the right size — so every other check in the
    decoder passes and the tile becomes terrain that looks entirely plausible.

    Three conditions make the fixture prove that and not something else. Same
    output LENGTH, or the row count would end the decode as a truncation. All
    filter bytes still 0-4, or the filter check would refuse it first. And
    different bytes, which is the whole point.
    """
    offset, length = find_chunk(data, b"IDAT")
    rows = len(expected_raw) // row_bytes
    # Never the last four bytes: the trailer has to stay the encoder's own, so
    # that the mismatch is the data having moved and not the checksum.
    for shift in range(2, length - 4):
        payload = bytearray(data[offset : offset + length])
        payload[shift] ^= 0xFF
        inflated = inflate_raw(bytes(payload))
        if inflated is None or len(inflated) != len(expected_raw):
            continue
        if inflated == expected_raw:
            continue
        if any(inflated[row * row_bytes] > 4 for row in range(rows)):
            continue
        return replace_idat(data, bytes(payload))
    raise AssertionError("no byte flip inflated to a same-length different raster")


def corrupt_adler_trailer(data: bytes) -> bytes:
    """Flip the last byte of the stream, i.e. the Adler-32 and nothing else."""
    stream = bytearray(idat_stream(data))
    stream[-1] ^= 0xFF
    return replace_idat(data, bytes(stream))


def drop_adler_trailer(data: bytes) -> bytes:
    """Cut the stream off exactly after the last scanline byte."""
    stream = idat_stream(data)
    _, _, trailer_offset = deflate_blocks(stream)
    assert trailer_offset == len(stream) - 4, "trailer is not the last four bytes"
    return replace_idat(data, stream[:trailer_offset])


# --- Reference decode, i.e. the self-check -----------------------------------


def unfilter_all(raw: bytes, width: int, height: int, bpp: int) -> bytes:
    """A filtered stream turned back into a raster, row by row."""
    stride = width * bpp
    assert len(raw) >= height * (1 + stride), "filtered stream is short"
    prior = bytes(stride)
    out = bytearray()
    for row in range(height):
        start = row * (1 + stride)
        line = unfilter_row(raw[start], raw[start + 1 : start + 1 + stride], prior, bpp)
        out += line
        prior = line
    return bytes(out)


def reference_decode(data: bytes) -> tuple[int, int, bytes]:
    """Decode our own output with zlib and the unfilter above.

    Deliberately independent of the assembly path: it walks the chunk list,
    verifies every CRC, concatenates whatever IDATs it finds and unfilters row
    by row. Anything the two disagree on is a bug in this script.
    """
    assert data[: len(SIGNATURE)] == SIGNATURE, "bad signature"
    pos = len(SIGNATURE)
    header: tuple[int, int, int, int] | None = None
    compressed = bytearray()
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        tag = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        assert len(payload) == length, f"{tag!r} payload runs past the end"
        (crc,) = struct.unpack(">I", data[pos + 8 + length : pos + 12 + length])
        assert crc == zlib.crc32(tag + payload) & 0xFFFFFFFF, f"{tag!r} CRC"
        if tag == b"IHDR":
            width, height, depth, colour_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            header = (width, height, colour_type, interlace)
            assert depth == 8, "fixtures are 8-bit"
        elif tag == b"IDAT":
            compressed += payload
        pos += 12 + length

    assert header is not None, "no IHDR"
    width, height, colour_type, interlace = header
    assert interlace == 0, "reference decode is non-interlaced only"
    bpp = BYTES_PER_PIXEL[colour_type]
    raw = zlib.decompress(bytes(compressed))

    assert len(raw) == height * (1 + width * bpp), "unexpected filtered length"
    return width, height, unfilter_all(raw, width, height, bpp)


def fnv1a64(data: bytes) -> int:
    digest = FNV_OFFSET_BASIS
    for byte in data:
        digest = ((digest ^ byte) * FNV_PRIME) & FNV_MASK
    return digest


def self_check(fixtures: list[Fixture]) -> None:
    """Every fixture is what it claims to be, before a line is written."""
    block_types = set()
    for fixture in fixtures:
        if fixture.rejected:
            check_rejected(fixture)
            continue
        assert fixture.pixels is not None, fixture.name
        width, height, decoded = reference_decode(fixture.png)
        assert (width, height) == (fixture.width, fixture.height), fixture.name
        assert len(fixture.pixels) == width * height * 3, fixture.name
        assert decoded == fixture.pixels, f"{fixture.name} raster mismatch"

        stream = idat_stream(fixture.png)
        block_type = first_block_type(stream)
        block_types.add(block_type)
        if fixture.block_type is not None:
            assert block_type == fixture.block_type, (
                f"{fixture.name} is BTYPE {block_type}, not {fixture.block_type}"
            )

        raw_len = height * (1 + width * BYTES_PER_PIXEL[COLOUR_TYPE_RGB])
        completing = raster_completing_block(stream, raw_len)
        assert completing.final == fixture.verifies_adler, (
            f"{fixture.name} completes its raster in a "
            f"{'final' if completing.final else 'non-final'} block, so the "
            f"Adler-32 check is {'reached' if completing.final else 'skipped'}"
        )

    # Stored, fixed and dynamic are three separate paths through an inflater,
    # and a fixture set that happened to cover only two of them would leave one
    # of them untested while looking complete.
    assert block_types >= {0, 1, 2}, f"DEFLATE block types covered: {block_types}"


def check_rejected(fixture: Fixture) -> None:
    """The reject fixtures are malformed in the way their name claims."""
    if fixture.name == "reject_bad_signature":
        assert fixture.png[: len(SIGNATURE)] != SIGNATURE
        return

    offset, length = find_chunk(fixture.png, b"IHDR")
    _, _, _, colour_type, _, _, interlace = struct.unpack(
        ">IIBBBBB", fixture.png[offset : offset + length]
    )
    if fixture.name == "reject_greyscale":
        assert colour_type == COLOUR_TYPE_GREY
        return
    if fixture.name == "reject_interlaced":
        assert interlace == 1
        return
    if fixture.name == "reject_truncated":
        idat_offset, idat_length = find_chunk(fixture.png, b"IDAT")
        assert len(fixture.png) < idat_offset + idat_length
        return
    if fixture.name in (
        "reject_wrong_adler",
        "reject_silent_corruption",
        "reject_missing_adler",
    ):
        check_adler_rejected(fixture)
        return
    if fixture.name == "reject_extra_scanline":
        check_extra_scanline(fixture)
        return
    if fixture.name == "reject_corrupt_deflate":
        idat_offset, idat_length = find_chunk(fixture.png, b"IDAT")
        payload = fixture.png[idat_offset : idat_offset + idat_length]
        assert zlib.crc32(b"IDAT" + payload) & 0xFFFFFFFF == struct.unpack(
            ">I", fixture.png[idat_offset + idat_length : idat_offset + idat_length + 4]
        )[0], "corrupt fixture must still carry a valid chunk CRC"
        assert inflate_raw(payload) is None, "corrupt fixture still inflates"
        return
    raise AssertionError(f"no self-check for {fixture.name}")


def check_extra_scanline(fixture: Fixture) -> None:
    """The overrun fixture overruns, and is otherwise beyond reproach.

    Each assertion here closes off one other way the decoder could refuse it. If
    any of them stopped holding, the fixture would still be rejected and its test
    would still pass, while no longer saying anything about the row-count guard.
    """
    assert fixture.expected_raw is not None, fixture.name
    stream = idat_stream(fixture.png)
    inflated = inflate_raw(stream)
    assert inflated is not None, "must inflate: nothing structural may refuse it"

    row_bytes = 1 + fixture.width * 3
    declared = len(fixture.expected_raw)
    assert declared == fixture.height * row_bytes, "expected_raw is not the raster"
    assert len(inflated) == declared + row_bytes, "not exactly one row too long"
    assert inflated[:declared] == fixture.expected_raw, "declared rows must be intact"
    assert inflated[declared] <= 4, "extra row's filter byte must not be the reason"

    # The checksum covers the over-long data, so it agrees with the stream and
    # cannot be what refuses the image.
    stored = struct.unpack(">I", stream[-4:])[0]
    assert stored == zlib.adler32(inflated) & 0xFFFFFFFF, "trailer must be valid"

    # And the rows it does declare are the pixels the suite will compare against.
    assert fixture.pixels is not None, fixture.name
    assert (
        unfilter_all(fixture.expected_raw, fixture.width, fixture.height, 3)
        == fixture.pixels
    ), "declared raster does not match the emitted expectation"


def check_adler_rejected(fixture: Fixture) -> None:
    """The checksum fixtures are damaged in the checksum, not in the structure.

    Each one has to fail for exactly one reason and no other, or it stops being
    evidence about the Adler-32 code: the image data must still inflate, the
    raster must still be complete, and the decoder must actually get as far as
    reading the trailer.
    """
    assert fixture.expected_raw is not None, fixture.name
    stream = idat_stream(fixture.png)
    inflated = inflate_raw(stream)
    assert inflated is not None, f"{fixture.name} no longer inflates"

    # Reaching the check is a precondition for every one of them: the decoder
    # only compares the trailer if the raster finished inside a BFINAL block.
    assert reaches_adler_check(stream, len(fixture.expected_raw)), (
        f"{fixture.name} never reaches the adler-32 check"
    )

    if fixture.name == "reject_missing_adler":
        assert inflated == fixture.expected_raw, "raster should be intact"
        _, _, trailer_offset = deflate_blocks(stream)
        assert trailer_offset == len(stream), "trailer bytes are still present"
        return

    stored = struct.unpack(">I", stream[-4:])[0]
    true_adler = zlib.adler32(fixture.expected_raw) & 0xFFFFFFFF

    if fixture.name == "reject_wrong_adler":
        assert inflated == fixture.expected_raw, "only the trailer may differ"
        assert stored != true_adler, "trailer still matches the data"
        return

    # reject_silent_corruption: the trailer is the encoder's own, the data moved.
    assert stored == true_adler, "trailer must be the untouched original"
    assert len(inflated) == len(fixture.expected_raw), "length must still match"
    assert inflated != fixture.expected_raw, "data is not actually corrupted"
    assert zlib.adler32(inflated) & 0xFFFFFFFF != stored, "checksum cannot catch it"


# --- Header rendering --------------------------------------------------------


def render_array(name: str, data: bytes) -> list[str]:
    lines = [f"constexpr uint8_t {name}[] = {{"]
    for start in range(0, len(data), BYTES_PER_LINE):
        chunk_bytes = data[start : start + BYTES_PER_LINE]
        lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in chunk_bytes) + ",")
    lines.append("};")
    return lines


def wrap_doc(doc: str, width: int = 76) -> list[str]:
    words = doc.split()
    lines: list[str] = []
    current = ""
    for word in words:
        candidate = f"{current} {word}".strip()
        if len(candidate) + 3 > width:
            lines.append(current)
            current = word
        else:
            current = candidate
    if current:
        lines.append(current)
    if len(lines) == 1:
        return [f"/** {lines[0]} */"]
    return ["/**", *[f" * {line}" for line in lines], " */"]


def render_fixture(fixture: Fixture) -> list[str]:
    lines = wrap_doc(fixture.doc)
    lines += render_array(f"k{fixture.symbol}Png", fixture.png)
    if fixture.emit_pixels:
        assert fixture.pixels is not None
        lines += render_array(f"k{fixture.symbol}Rgb", fixture.pixels)
        rgb = [f"    k{fixture.symbol}Rgb,", f"    sizeof(k{fixture.symbol}Rgb),"]
    else:
        rgb = ["    nullptr,", "    0,"]
    lines += [
        f"constexpr Fixture k{fixture.symbol} = {{",
        f'    "{fixture.name}",',
        f"    k{fixture.symbol}Png,",
        f"    sizeof(k{fixture.symbol}Png),",
        f"    {fixture.width},",
        f"    {fixture.height},",
        *rgb,
        "};",
        "",
    ]
    return lines


def render_header(fixtures: list[Fixture]) -> str:
    decodable = [f for f in fixtures if not f.rejected]
    rejected = [f for f in fixtures if f.rejected]
    tile = next(f for f in fixtures if f.name == "terrarium_tile")
    assert tile.pixels is not None
    tile_stride = tile.width * 3

    lines = [
        "// Generated by scripts/gen_png_fixtures.py — do not edit.",
        "//",
        "// Regenerate with: python3 scripts/gen_png_fixtures.py",
        "//",
        "// Each fixture carries the PNG bytes AND the RGB raster they must decode",
        "// to, so test_png asserts pixel equality rather than absence of a crash.",
        "// The generator decodes its own output with zlib before emitting any of",
        "// this, so a mismatch here is a decoder bug, never a fixture bug.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace png_fixtures {",
        "",
        "/** A PNG and what it has to decode to. */",
        "struct Fixture {",
        "  const char* name;  ///< appears in the failure message",
        "  const uint8_t* png;",
        "  size_t png_len;",
        "  uint32_t width;",
        "  uint32_t height;",
        "  /** width*height*3 in raster order, or nullptr when not emitted. */",
        "  const uint8_t* rgb;",
        "  size_t rgb_len;",
        "};",
        "",
    ]

    for fixture in fixtures:
        lines += render_fixture(fixture)

    lines += [
        "/**",
        " * The 256x256 tile's raster is 196608 bytes, which as a C array would be",
        " * larger than everything else here put together. Hashing it instead is no",
        " * weaker a check: FNV-1a over the RGB bytes in the order the callbacks",
        " * arrived pins down both the values and the raster order exactly. The two",
        " * emitted rows are there to make a failure readable.",
        " */",
        f"constexpr uint64_t kFnvOffsetBasis = 0x{FNV_OFFSET_BASIS:016X}ULL;",
        f"constexpr uint64_t kFnvPrime = 0x{FNV_PRIME:X}ULL;",
        f"constexpr uint64_t kTerrariumTileRgbHash = "
        f"0x{fnv1a64(tile.pixels):016X}ULL;",
    ]
    lines += render_array("kTerrariumTileFirstRowRgb", tile.pixels[:tile_stride])
    lines += render_array(
        "kTerrariumTileLastRowRgb", tile.pixels[(tile.height - 1) * tile_stride :]
    )
    lines += [""]

    lines += [
        "/** Everything that must decode, for the properties that hold for all. */",
        "constexpr Fixture kDecodable[] = {",
    ]
    lines += [f"    k{f.symbol}," for f in decodable]
    lines += [
        "};",
        "",
        "/** Everything decode() must refuse without crashing. */",
        "constexpr Fixture kRejected[] = {",
    ]
    lines += [f"    k{f.symbol}," for f in rejected]
    lines += [
        "};",
        "",
        "}  // namespace png_fixtures",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    fixtures = build_fixtures()
    self_check(fixtures)

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(render_header(fixtures), encoding="utf-8")

    for fixture in fixtures:
        kind = "reject" if fixture.rejected else "decode"
        detail = ""
        if not fixture.rejected:
            adler = "adler checked" if fixture.verifies_adler else "adler SKIPPED"
            detail = (
                f"  BTYPE {first_block_type(idat_stream(fixture.png))}  {adler}"
            )
        print(
            f"{fixture.name:<26} {kind}  {fixture.width}x{fixture.height}"
            f"  {len(fixture.png)} PNG bytes{detail}"
        )
    print(f"wrote {OUT_H.relative_to(ROOT)} ({OUT_H.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
