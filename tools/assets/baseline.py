#!/usr/bin/env python3
"""Nearest-neighbour upscale of extracted frames: the reference "candidate" every real upscaler must beat, and
a way to exercise the gates and comparison sheets end to end.

    python tools/assets/baseline.py ~/.ja2-assets/extracted/Faces ~/.ja2-assets/hd-baseline/Faces --scale 4
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ja2assets import png  # noqa: E402


def nearest(img: png.Image, k: int) -> png.Image:
    out = png.Image.blank(img.width * k, img.height * k)
    for y in range(img.height):
        row = bytearray()
        for x in range(img.width):
            i = (y * img.width + x) * 4
            row += bytes(img.rgba[i:i + 4]) * k
        for dy in range(k):
            o = (y * k + dy) * out.width * 4
            out.rgba[o:o + len(row)] = row
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path)
    ap.add_argument("dst", type=Path)
    ap.add_argument("--scale", type=int, default=4)
    args = ap.parse_args(argv)
    n = 0
    for f in sorted(args.src.rglob("frame_*.png")):
        out = args.dst / f.relative_to(args.src)
        out.parent.mkdir(parents=True, exist_ok=True)
        png.write(out, nearest(png.read(f), args.scale))
        n += 1
    print(f"{n} frames x{args.scale} -> {args.dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
