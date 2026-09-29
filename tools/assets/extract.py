#!/usr/bin/env python3
"""Extracts the game's images (STI, PCX) from the player's .slf archives (and loose files in Data/) to PNG + JSON.

    python tools/assets/extract.py                          # everything, to ~/.ja2-assets/extracted
    python tools/assets/extract.py --archive Interface --archive Faces --out /tmp/x
    python tools/assets/extract.py --list                   # just list the images

Per image, in <out>/<Data-relative path>/: one PNG per frame (frame_000.png, ...) with transparency, and
meta.json with the frames (size, offsets), the palette (hex), the STCI flags and the application data (raw hex
and, when it is AuxObjectData, decoded). The work directory is outside git on purpose: the art belongs to the
rights holders and derived images never enter the repository (docs/plan/native-modern-game.md, Licensing).
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ja2assets import DEFAULT_WORK_DIR, find_data_dir  # noqa: E402
from ja2assets import pcx, png, sti  # noqa: E402
from ja2assets.slf import SlfArchive  # noqa: E402

IMAGE_EXTS = (".sti", ".pcx")


def iter_sources(data_dir: Path, archives: list[str] | None):
    """(Data-relative path, loader) for every image, archives first, then loose files (which override them)."""
    wanted = {a.lower().removesuffix(".slf") for a in archives} if archives else None
    for slf in sorted(data_dir.glob("*.slf"), key=lambda p: p.name.lower()):
        if wanted is not None and slf.stem.lower() not in wanted:
            continue
        with SlfArchive(slf) as a:
            for e in a.files():
                if e.name.lower().endswith(IMAGE_EXTS):
                    # the loader is only valid until the next item (the archive stays open while iterating)
                    yield slf.name, a.full_path(e), (lambda a=a, e=e: a.read(e))
    if wanted is None:
        for f in sorted(data_dir.rglob("*")):
            if f.is_file() and f.suffix.lower() in IMAGE_EXTS:
                yield "(loose)", f.relative_to(data_dir).as_posix(), (lambda f=f: f.read_bytes())


def extract_one(rel: str, data: bytes, out: Path) -> dict:
    d = out / rel
    d.mkdir(parents=True, exist_ok=True)
    meta: dict = {"source": rel}
    if rel.lower().endswith(".pcx"):
        img, palette = pcx.parse(data)
        png.write(d / "frame_000.png", img)
        meta.update(format="PCX", width=img.width, height=img.height,
                    frames=[{"file": "frame_000.png", "width": img.width, "height": img.height, "offsetX": 0, "offsetY": 0}],
                    palette=["%02x%02x%02x" % c for c in palette])
    else:
        s = sti.parse(data)
        meta.update(format="STI", width=s.width, height=s.height, depth=s.depth, flags=s.flags,
                    indexed=s.indexed, transparent=s.transparent, frames=[])
        if s.palette:
            meta["palette"] = ["%02x%02x%02x" % c for c in s.palette]
        for i, f in enumerate(s.frames):
            name = f"frame_{i:03d}.png"
            png.write(d / name, s.frame_rgba(i))
            meta["frames"].append({"file": name, "width": f.width, "height": f.height,
                                   "offsetX": f.offset_x, "offsetY": f.offset_y})
        if s.app_data:
            meta["appData"] = s.app_data.hex()
            aux = s.aux_objects()
            if aux:
                meta["auxObjects"] = aux
    (d / "meta.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    return meta


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, help="the game's Data directory (default: from ja2.json)")
    ap.add_argument("--out", type=Path, default=DEFAULT_WORK_DIR / "extracted")
    ap.add_argument("--archive", action="append", help="only this archive (e.g. Interface); repeatable")
    ap.add_argument("--limit", type=int, default=0, help="stop after N images")
    ap.add_argument("--list", action="store_true", help="list images, extract nothing")
    args = ap.parse_args(argv)
    data_dir = args.data or find_data_dir()
    if not data_dir or not data_dir.is_dir():
        print("error: no game Data directory (use --data or set game_dir in ja2.json)", file=sys.stderr)
        return 2
    n = failed = 0
    t0 = time.time()
    for archive, rel, load in iter_sources(data_dir, args.archive):
        if args.list:
            print(f"{archive}\t{rel}")
        else:
            try:
                extract_one(rel, load(), args.out)
            except Exception as e:  # report and carry on: one odd file should not stop a 7,500-file run
                failed += 1
                print(f"warning: {rel}: {e}", file=sys.stderr)
        n += 1
        if args.limit and n >= args.limit:
            break
    if not args.list:
        print(f"{n} images ({failed} failed) -> {args.out} in {time.time() - t0:.1f} s")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
