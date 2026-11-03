#!/usr/bin/env python3
"""Rasterize BIZ UDGothic into the bitmap-glyph format used by apps/yaft.

Output (cjk32.bin, little endian):
  "YGL1", u32 count, then `count` records sorted by codepoint:
    u32 code, u8 width (1 or 2 cells), 3 pad bytes, u32 bitmap[32]
  which is exactly sizeof(glyph_t) == 136 bytes (apps/yaft/glyph.h).

Bit order (see ScreenRenderObject::drawLine): row `h` of a glyph is a
word whose bit (cells * 16 - 1 - x) is pixel x, so the leftmost pixel is
the most significant bit of the cell (bit 15 for 1 cell, bit 31 for 2).

Needs python3 + Pillow. Usage: gen_cjk_glyphs.py [--out cjk32.bin]
"""

import argparse
import hashlib
import struct
import sys
import unicodedata
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent

# google/fonts commit that last touched the font (Version 1.05).
FONT_COMMIT = "01ac47e0b1cddbc298334c69a5dfafb0205ffa28"
FONT_REPO_PATH = "ofl/bizudgothic/BIZUDGothic-Regular.ttf"
FONT_URL = (
    "https://raw.githubusercontent.com/google/fonts/"
    f"{FONT_COMMIT}/{FONT_REPO_PATH}"
)
FONT_SHA256 = "709fcd41e3209fb765da750472f55ccdf925653e9fa7e1eb007cb65c8f749c75"

CELL_W = 16
CELL_H = 32

# The builtin ASCII glyphs have their baseline between rows 25 and 26 (caps
# span rows 6..25). A 30px em with the baseline there keeps ideographs inside
# rows 2..28 and lines them up with the Latin text.
FONT_SIZE = 30
BASELINE = 26
THRESHOLD = 110

WIDE_RANGES = [
    (0x3000, 0x30FF),
    (0x31F0, 0x31FF),
    (0x3200, 0x33FF),
    (0x4E00, 0x9FFF),
    (0xF900, 0xFAFF),
    (0xFF01, 0xFF60),
    (0xFFE0, 0xFFE6),
]
HALF_RANGES = [(0xFF61, 0xFF9F)]


def fetch_font(cache: Path) -> Path:
    if not cache.exists():
        print(f"downloading {FONT_URL}", file=sys.stderr)
        with urllib.request.urlopen(FONT_URL) as resp:
            cache.write_bytes(resp.read())
    digest = hashlib.sha256(cache.read_bytes()).hexdigest()
    if digest != FONT_SHA256:
        sys.exit(f"{cache}: sha256 {digest} != pinned {FONT_SHA256}")
    return cache


def read_cmap(data: bytes) -> set:
    """Returns the set of codepoints mapped by the font's best Unicode cmap."""

    def u16(off):
        return struct.unpack_from(">H", data, off)[0]

    def u32(off):
        return struct.unpack_from(">I", data, off)[0]

    num_tables = u16(4)
    cmap_off = None
    for i in range(num_tables):
        rec = 12 + 16 * i
        if data[rec : rec + 4] == b"cmap":
            cmap_off = u32(rec + 8)
    if cmap_off is None:
        sys.exit("no cmap table")

    subtables = []
    for i in range(u16(cmap_off + 2)):
        rec = cmap_off + 4 + 8 * i
        subtables.append((u16(rec), u16(rec + 2), cmap_off + u32(rec + 4)))
    # Prefer full-repertoire UCS-4, then BMP-only Unicode subtables.
    for want in [(3, 10), (0, 4), (0, 3), (3, 1)]:
        for platform, encoding, off in subtables:
            if (platform, encoding) == want and u16(off) in (4, 12):
                return parse_cmap_subtable(data, off)
    sys.exit("no usable Unicode cmap subtable")


def parse_cmap_subtable(data: bytes, off: int) -> set:
    fmt = struct.unpack_from(">H", data, off)[0]
    codes = set()
    if fmt == 12:
        n_groups = struct.unpack_from(">I", data, off + 12)[0]
        for i in range(n_groups):
            start, end, _ = struct.unpack_from(">III", data, off + 16 + 12 * i)
            codes.update(range(start, end + 1))
    elif fmt == 4:
        seg_x2 = struct.unpack_from(">H", data, off + 6)[0]
        n = seg_x2 // 2
        ends = struct.unpack_from(f">{n}H", data, off + 14)
        starts = struct.unpack_from(f">{n}H", data, off + 16 + seg_x2)
        deltas = struct.unpack_from(f">{n}h", data, off + 16 + 2 * seg_x2)
        offsets_pos = off + 16 + 3 * seg_x2
        range_offsets = struct.unpack_from(f">{n}H", data, offsets_pos)
        for i in range(n):
            for code in range(starts[i], ends[i] + 1):
                if code == 0xFFFF:
                    continue
                if range_offsets[i] == 0:
                    gid = (code + deltas[i]) & 0xFFFF
                else:
                    pos = offsets_pos + 2 * i + range_offsets[i] + 2 * (code - starts[i])
                    gid = struct.unpack_from(">H", data, pos)[0]
                    if gid:
                        gid = (gid + deltas[i]) & 0xFFFF
                if gid:
                    codes.add(code)
    return codes


def cell_width(code: int):
    """Cells a terminal gives `code`, or None if we should not draw it."""
    ch = chr(code)
    if unicodedata.category(ch)[0] in "MC" or unicodedata.category(ch) == "Zl":
        return None
    eaw = unicodedata.east_asian_width(ch)
    if any(a <= code <= b for a, b in WIDE_RANGES):
        # Neutral/ambiguous characters are one cell wide in a terminal, so a
        # 2-cell glyph would be rejected (and replaced by '?') at draw time.
        return 2 if eaw in ("W", "F") else None
    if any(a <= code <= b for a, b in HALF_RANGES):
        return 1 if eaw == "H" else None
    return None


def rasterize(font, code: int, width: int) -> list:
    w = CELL_W * width
    img = Image.new("L", (w, CELL_H), 0)
    draw = ImageDraw.Draw(img)
    advance = font.getlength(chr(code))
    x0 = int((w - advance) // 2)
    draw.text((x0, BASELINE), chr(code), font=font, fill=255, anchor="ls")

    px = img.load()
    rows = []
    for y in range(CELL_H):
        word = 0
        for x in range(w):
            if px[x, y] >= THRESHOLD:
                word |= 1 << (w - 1 - x)
        rows.append(word)
    return rows


def build(font_path: Path) -> bytes:
    data = font_path.read_bytes()
    mapped = read_cmap(data)
    font = ImageFont.truetype(str(font_path), FONT_SIZE)

    records = []
    for code in sorted(mapped):
        width = cell_width(code)
        if width is None:
            continue
        rows = rasterize(font, code, width)
        records.append(struct.pack("<IB3x32I", code, width, *rows))

    return b"YGL1" + struct.pack("<I", len(records)) + b"".join(records)


def preview(blob: bytes, out: Path, text: str):
    """Writes a 3x scaled sheet of `text` set in the generated glyphs."""
    count = struct.unpack_from("<I", blob, 4)[0]
    glyphs = {}
    for i in range(count):
        rec = struct.unpack_from("<IB3x32I", blob, 8 + 136 * i)
        glyphs[rec[0]] = (rec[1], rec[2:])
    cols = sum(glyphs[ord(c)][0] for c in text if ord(c) in glyphs)
    img = Image.new("L", (cols * CELL_W, CELL_H), 255)
    px = img.load()
    cx = 0
    for c in text:
        if ord(c) not in glyphs:
            continue
        width, rows = glyphs[ord(c)]
        for y, word in enumerate(rows):
            for x in range(width * CELL_W):
                if word & (1 << (width * CELL_W - 1 - x)):
                    px[cx * CELL_W + x, y] = 0
        cx += width
    img.resize((img.width * 3, img.height * 3), Image.NEAREST).save(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=HERE / "cjk32.bin")
    ap.add_argument(
        "--font-cache",
        type=Path,
        default=Path("/tmp") / f"BIZUDGothic-Regular-{FONT_COMMIT[:8]}.ttf",
        help="where the pinned font download is kept",
    )
    ap.add_argument("--preview", type=Path, help="also write a PNG sample sheet")
    ap.add_argument("--preview-text", default="日本語の入力、漢字かなカナｱｲｳ")
    args = ap.parse_args()

    blob = build(fetch_font(args.font_cache))
    args.out.write_bytes(blob)
    count = struct.unpack_from("<I", blob, 4)[0]
    print(f"{args.out}: {count} glyphs, {len(blob)} bytes")
    print(f"sha256 {hashlib.sha256(blob).hexdigest()}")
    if args.preview:
        preview(blob, args.preview, args.preview_text)


if __name__ == "__main__":
    main()
