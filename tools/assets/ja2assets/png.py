"""Minimal PNG encode/decode for 8-bit RGBA (standard library only, like tools/ja2ctl.py)."""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Image:
    """8-bit RGBA, rows top to bottom, 4 bytes per pixel."""
    width: int
    height: int
    rgba: bytearray

    @staticmethod
    def blank(width: int, height: int) -> "Image":
        return Image(width, height, bytearray(width * height * 4))

    def pixel(self, x: int, y: int) -> tuple[int, int, int, int]:
        i = (y * self.width + x) * 4
        return tuple(self.rgba[i:i + 4])  # type: ignore[return-value]

    def set_pixel(self, x: int, y: int, p) -> None:
        i = (y * self.width + x) * 4
        self.rgba[i:i + 4] = bytes(p)


def _chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def encode(img: Image) -> bytes:
    stride = img.width * 4
    raw = bytearray()
    for y in range(img.height):
        raw.append(0)
        raw += img.rgba[y * stride:(y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", img.width, img.height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + _chunk(b"IEND", b"")


def write(path: str | Path, img: Image) -> None:
    Path(path).write_bytes(encode(img))


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode(data: bytes) -> Image:
    """Decodes 8-bit greyscale, RGB, RGBA, grey+alpha and palette PNGs (no interlacing) to RGBA."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    idat = bytearray()
    palette = b""
    trns = b""
    width = height = depth = ctype = 0
    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace:
                raise ValueError("only 8-bit, non-interlaced PNGs are supported")
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    out = bytearray(width * height * 4)
    prev = bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 255
        prev = line
        for x in range(width):
            o = (y * width + x) * 4
            px = line[x * channels:(x + 1) * channels]
            if ctype == 6:
                out[o:o + 4] = px
            elif ctype == 2:
                out[o:o + 4] = bytes(px) + b"\xff"
            elif ctype == 0:
                out[o:o + 4] = bytes((px[0], px[0], px[0], 255))
            elif ctype == 4:
                out[o:o + 4] = bytes((px[0], px[0], px[0], px[1]))
            else:
                k = px[0]
                out[o:o + 3] = palette[k * 3:k * 3 + 3]
                out[o + 3] = trns[k] if k < len(trns) else 255
    return Image(width, height, out)


def read(path: str | Path) -> Image:
    return decode(Path(path).read_bytes())
