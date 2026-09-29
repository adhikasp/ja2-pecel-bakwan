"""STCI ("STI") images, as src/sgp/STCI.cc and ImgFmt.h read them.

  header, 64 bytes: "STCI", uint32 original size, uint32 stored size, uint32 transparent value, uint32 flags,
    uint16 height, uint16 width, then a 20-byte union:
      RGB:     uint32 red/green/blue/alpha masks, uint8 red/green/blue/alpha depths
      indexed: uint32 colour count, uint16 sub-image count, uint8 r/g/b depths, 11 unused
    uint8 depth (offset 44), 3 pad, uint32 app data size (offset 48), 12 unused
  indexed: palette (colour count x 3 bytes RGB), sub-image table (16 bytes each: uint32 offset, uint32 length,
           int16 x offset, int16 y offset, uint16 height, uint16 width) when ETRLE compressed
  image data (stored size bytes), then app data (app data size bytes; 16-byte AuxObjectData per sub-image
  for tiles and animations)
ETRLE rows: a byte with the high bit set skips (byte & 0x7F) transparent pixels, another non-zero byte n is
followed by n palette indices, 0 ends the row.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from .png import Image

ETRLE_COMPRESSED = 0x0020
ZLIB_COMPRESSED = 0x0010
INDEXED = 0x0008
RGB = 0x0004
ALPHA = 0x0002
TRANSPARENT = 0x0001


@dataclass
class Frame:
    width: int
    height: int
    offset_x: int
    offset_y: int
    indices: bytes          # width*height palette indices (indexed images)
    mask: bytes             # 1 = opaque
    rgb565: list[int] | None = None  # 16-bit images


@dataclass
class Sti:
    width: int
    height: int
    flags: int
    depth: int
    transparent: int
    palette: list[tuple[int, int, int]] = field(default_factory=list)
    frames: list[Frame] = field(default_factory=list)
    app_data: bytes = b""

    @property
    def indexed(self) -> bool:
        return bool(self.flags & INDEXED)

    def aux_objects(self) -> list[dict]:
        """AuxObjectData per sub-image (HImage.h), when the app data holds exactly that."""
        n = len(self.frames)
        if not n or len(self.app_data) != 16 * n:
            return []
        out = []
        for i in range(n):
            wall, tiles, loc = struct.unpack_from("<BBH", self.app_data, i * 16)
            cur, count, flags = struct.unpack_from("<BBB", self.app_data, i * 16 + 7)
            out.append({"wallOrientation": wall, "numberOfTiles": tiles, "tileLocIndex": loc,
                        "currentFrame": cur, "numberOfFrames": count, "flags": flags})
        return out

    def frame_rgba(self, i: int) -> Image:
        f = self.frames[i]
        img = Image.blank(f.width, f.height)
        px = img.rgba
        if f.rgb565 is not None:
            for k, p in enumerate(f.rgb565):
                r, g, b = (p >> 11) & 31, (p >> 5) & 63, p & 31
                px[k * 4:k * 4 + 4] = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255))
            return img
        pal = self.palette
        for k, (idx, m) in enumerate(zip(f.indices, f.mask)):
            if m:
                r, g, b = pal[idx] if idx < len(pal) else (255, 0, 255)
                px[k * 4:k * 4 + 4] = bytes((r, g, b, 255))
        return img


def decode_etrle(data: bytes, width: int, height: int) -> tuple[bytes, bytes]:
    indices = bytearray(width * height)
    mask = bytearray(width * height)
    p = 0
    n = len(data)
    for y in range(height):
        x = 0
        while p < n:
            b = data[p]
            p += 1
            if b == 0:
                break
            if b & 0x80:
                x += b & 0x7F
            else:
                run = data[p:p + b]
                p += b
                end = min(x + b, width)
                if end > x:
                    o = y * width
                    indices[o + x:o + end] = run[:end - x]
                    mask[o + x:o + end] = b"\x01" * (end - x)
                x += b
    return bytes(indices), bytes(mask)


def encode_etrle(indices: bytes, mask: bytes, width: int, height: int) -> bytes:
    """The inverse of decode_etrle (for synthetic test images)."""
    out = bytearray()
    for y in range(height):
        x = 0
        while x < width:
            opaque = mask[y * width + x]
            run = 0
            while x + run < width and mask[y * width + x + run] == opaque and run < 0x7F:
                run += 1
            if opaque:
                out.append(run)
                out += indices[y * width + x:y * width + x + run]
            else:
                out.append(0x80 | run)
            x += run
        out.append(0)
    return bytes(out)


def parse(data: bytes) -> Sti:
    if data[:4] != b"STCI":
        raise ValueError("not an STCI file")
    _orig, stored, transparent, flags, height, width = struct.unpack_from("<IIIIHH", data, 4)
    depth = data[44]
    (app_size,) = struct.unpack_from("<I", data, 48)
    if flags & ZLIB_COMPRESSED:
        raise ValueError("zlib-compressed STCI is not supported (neither is it by the game)")
    sti = Sti(width, height, flags, depth, transparent)
    pos = 64
    if flags & INDEXED:
        colours, subimages = struct.unpack_from("<IH", data, 24)
        for i in range(colours):
            sti.palette.append(tuple(data[pos + i * 3:pos + i * 3 + 3]))  # type: ignore[arg-type]
        pos += colours * 3
        if flags & ETRLE_COMPRESSED:
            table = []
            for i in range(subimages):
                table.append(struct.unpack_from("<IIhhHH", data, pos + i * 16))
            pos += subimages * 16
            pixels = data[pos:pos + stored]
            for off, length, ox, oy, h, w in table:
                idx, mask = decode_etrle(pixels[off:off + length], w, h)
                sti.frames.append(Frame(w, h, ox, oy, idx, mask))
        else:
            pixels = data[pos:pos + width * height]
            if flags & TRANSPARENT:
                mask = bytes(0 if p == (transparent & 0xFF) else 1 for p in pixels)
            else:
                mask = b"\x01" * (width * height)
            sti.frames.append(Frame(width, height, 0, 0, bytes(pixels), mask))
        pos += stored
    elif flags & RGB:
        if depth != 16:
            raise ValueError(f"unsupported RGB depth {depth}")
        count = width * height
        values = list(struct.unpack_from(f"<{count}H", data, pos))
        sti.frames.append(Frame(width, height, 0, 0, b"", b"\x01" * count, values))
        pos += stored
    else:
        raise ValueError("unknown STCI data organisation")
    sti.app_data = data[pos:pos + app_size]
    return sti


def build_indexed(frames: list[tuple[int, int, int, int, bytes, bytes]], palette: list[tuple[int, int, int]],
                  app_data: bytes = b"") -> bytes:
    """Builds an ETRLE STI from (w, h, offset_x, offset_y, indices, mask) frames (for tests)."""
    table = bytearray()
    pixels = bytearray()
    for w, h, ox, oy, idx, mask in frames:
        enc = encode_etrle(idx, mask, w, h)
        table += struct.pack("<IIhhHH", len(pixels), len(enc), ox, oy, h, w)
        pixels += enc
    header = bytearray(64)
    header[0:4] = b"STCI"
    width = max(f[0] for f in frames)
    height = max(f[1] for f in frames)
    struct.pack_into("<IIIIHH", header, 4, len(pixels), len(pixels), 0, INDEXED | ETRLE_COMPRESSED | TRANSPARENT, height, width)
    struct.pack_into("<IH", header, 24, 256, len(frames))
    header[44] = 8
    struct.pack_into("<I", header, 48, len(app_data))
    pal = bytearray(768)
    for i, (r, g, b) in enumerate(palette[:256]):
        pal[i * 3:i * 3 + 3] = bytes((r, g, b))
    return bytes(header) + bytes(pal) + bytes(table) + bytes(pixels) + app_data
