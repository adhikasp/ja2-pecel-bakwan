"""8-bit PCX (ZSoft, RLE, 256-colour palette at the end), as src/sgp/PCX.cc reads them."""
from __future__ import annotations

import struct

from .png import Image


def parse(data: bytes) -> tuple[Image, list[tuple[int, int, int]]]:
    if len(data) < 128 + 769 or data[0] != 0x0A:
        raise ValueError("not a PCX file")
    bpp = data[3]
    xmin, ymin, xmax, ymax = struct.unpack_from("<HHHH", data, 4)
    planes = data[65]
    (bytes_per_line,) = struct.unpack_from("<H", data, 66)
    if bpp != 8 or planes != 1:
        raise ValueError("only 8-bit single-plane PCX is supported")
    width, height = xmax - xmin + 1, ymax - ymin + 1
    if data[-769] != 0x0C:
        raise ValueError("PCX has no 256-colour palette")
    pal_raw = data[-768:]
    palette = [tuple(pal_raw[i * 3:i * 3 + 3]) for i in range(256)]
    img = Image.blank(width, height)
    p = 128
    end = len(data) - 769
    for y in range(height):
        line = bytearray()
        while len(line) < bytes_per_line and p < end:
            b = data[p]
            p += 1
            if b & 0xC0 == 0xC0:
                count = b & 0x3F
                line += bytes((data[p],)) * count
                p += 1
            else:
                line.append(b)
        for x in range(width):
            r, g, b_ = palette[line[x]] if x < len(line) else (0, 0, 0)
            o = (y * width + x) * 4
            img.rgba[o:o + 4] = bytes((r, g, b_, 255))
    return img, palette  # type: ignore[return-value]


def build(width: int, height: int, indices: bytes, palette: list[tuple[int, int, int]]) -> bytes:
    """Writes an 8-bit PCX (for tests)."""
    header = bytearray(128)
    header[0], header[1], header[2], header[3] = 0x0A, 5, 1, 8
    struct.pack_into("<HHHH", header, 4, 0, 0, width - 1, height - 1)
    header[65] = 1
    struct.pack_into("<H", header, 66, width)
    body = bytearray()
    for y in range(height):
        for v in indices[y * width:(y + 1) * width]:
            if v >= 0xC0:
                body += bytes((0xC1, v))
            else:
                body.append(v)
    pal = bytearray(768)
    for i, (r, g, b) in enumerate(palette[:256]):
        pal[i * 3:i * 3 + 3] = bytes((r, g, b))
    return bytes(header) + bytes(body) + b"\x0c" + bytes(pal)
