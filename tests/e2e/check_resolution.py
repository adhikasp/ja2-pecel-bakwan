#!/usr/bin/env python3
"""Run an e2e tour at a given resolution and compare its screenshots with the
golden images in tests/e2e/golden/<res>/<script>/.

    python tests/e2e/check_resolution.py SCRIPT RES [--update] [--tolerance N] [--max-diff-pct P]

The tour itself also calls ja2.assertInsideScreen() (see lib/shots.lua), so a
layout bug at a wide resolution fails even before any pixels are compared.

A pixel differs when any channel differs by more than --tolerance (default 8,
out of 255). The screenshot fails when more than --max-diff-pct percent of its
pixels differ (default 0.05). --update (or JA2_UPDATE_GOLDEN=1) writes the
screenshots as the new golden images instead of comparing.

Golden images are only stored for the shots a script marks with
shots.golden(); other screenshots are still taken (and layout-checked) but
never compared. See tests/e2e/golden/README.md.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
JA2CTL = HERE.parent.parent / "tools" / "ja2ctl.py"
GOLDEN = HERE / "golden"


def read_png(path: Path):
    """Decode an 8-bit RGB/RGBA non-interlaced PNG -> (w, h, channels, rows)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, type {ctype})")
            ch = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = w * ch
    rows, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(ch, stride):
                line[i] = (line[i] + line[i - ch]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line)
        prev = line
    return w, h, ch, rows


def compare(a: Path, b: Path, tol: int):
    """Returns (differing_pixels, total_pixels) or None if sizes differ."""
    wa, ha, ca, ra = read_png(a)
    wb, hb, cb, rb = read_png(b)
    if (wa, ha) != (wb, hb):
        return None
    bad = 0
    # The main menu prints the build's version string in the bottom-left corner: don't compare it.
    mask_x = 260 if "main_menu" in a.name else 0
    for y, (la, lb) in enumerate(zip(ra, rb)):
        if la == lb:
            continue
        masked = y >= ha - 16
        for x in range(mask_x if masked else 0, wa):
            pa, pb = x * ca, x * cb
            if (abs(la[pa] - lb[pb]) > tol or abs(la[pa + 1] - lb[pb + 1]) > tol
                    or abs(la[pa + 2] - lb[pb + 2]) > tol):
                bad += 1
    return bad, wa * ha


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("res", help="e.g. 1280x720")
    ap.add_argument("--update", action="store_true", default=bool(os.environ.get("JA2_UPDATE_GOLDEN")))
    ap.add_argument("--tolerance", type=int, default=8)
    ap.add_argument("--max-diff-pct", type=float, default=0.05)
    ap.add_argument("--out", help="keep screenshots here (default: temp dir)")
    opts = ap.parse_args()

    script = Path(opts.script).resolve()
    name = script.stem
    gold_dir = GOLDEN / opts.res / name
    tmp = None
    out = Path(opts.out) if opts.out else None
    if out is None:
        tmp = tempfile.mkdtemp(prefix="ja2-res-")
        out = Path(tmp)
    out.mkdir(parents=True, exist_ok=True)
    (out / "golden.txt").unlink(missing_ok=True)
    try:
        cmd = [sys.executable, str(JA2CTL), "run", str(script), "--isolated", "--seed", "1",
               "--res", opts.res, "--timeout", "2400", "--out", str(out), "--arg", "golden=1"]
        code = subprocess.call(cmd)
        if code != 0:
            print(f"{name} @ {opts.res}: script failed with exit code {code}", file=sys.stderr)
            return code
        marked = out / "golden.txt"
        golden_shots = marked.read_text().split() if marked.exists() else []
        if not golden_shots:
            print(f"{name} @ {opts.res}: passed (no golden shots marked)")
            return 0
        if opts.update:
            gold_dir.mkdir(parents=True, exist_ok=True)
            for s in golden_shots:
                shutil.copyfile(out / s, gold_dir / s)
            print(f"{name} @ {opts.res}: updated {len(golden_shots)} golden image(s) in {gold_dir}")
            return 0
        failed = []
        for s in golden_shots:
            ref = gold_dir / s
            if not ref.exists():
                failed.append(f"{s}: no golden image at {ref} (run with --update)")
                continue
            r = compare(out / s, ref, opts.tolerance)
            if r is None:
                failed.append(f"{s}: size differs from golden")
                continue
            bad, total = r
            pct = 100.0 * bad / total
            if pct > opts.max_diff_pct:
                failed.append(f"{s}: {bad} px ({pct:.3f}%) differ, limit {opts.max_diff_pct}%")
        if failed:
            if not opts.out:
                keep = Path(tempfile.gettempdir()) / "ja2-resolution-failures" / opts.res / name
                keep.mkdir(parents=True, exist_ok=True)
                for s in golden_shots:
                    shutil.copyfile(out / s, keep / s)
                print(f"actual screenshots kept in {keep}", file=sys.stderr)
            print(f"{name} @ {opts.res}: golden mismatch:\n  " + "\n  ".join(failed), file=sys.stderr)
            return 1
        print(f"{name} @ {opts.res}: passed ({len(golden_shots)} golden image(s) match)")
        return 0
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
