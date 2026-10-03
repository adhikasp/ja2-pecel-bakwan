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

When the shot is not byte-identical to its golden, the comparison decodes both
PNGs. Pillow (PIL.Image) decodes them and numpy does the per-pixel compare when
both are importable — the pure-Python decoder below is ~50x slower at
widescreen sizes, so this is a large win on the fallback path. Neither is a hard
dependency: without them the harness still runs, just slower.

With --update, a script that cannot mark a golden image at this resolution
marks none, see expected_goldens()) is skipped instead of run: it would cost
minutes of tour to write nothing. The plain comparison run still runs it,
because it still checks the layout.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

# Optional accelerators for the compare fallback. Neither is required: the pure-Python decoder and
# compare loop below keep the harness runnable with only the standard library. See golden/README.md.
try:
    from PIL import Image as _Image
except ImportError:
    _Image = None
try:
    import numpy as _np
except ImportError:
    _np = None

HERE = Path(__file__).resolve().parent
JA2CTL = HERE.parent.parent / "tools" / "ja2ctl.py"
GOLDEN = HERE / "golden"


def _take_args(text: str, start: int):
    """The arguments of a call whose '(' sits just before @a start, split at top-level commas.

    String literals are taken verbatim (their parentheses and commas do not count) and the call's own
    closing ')' ends the list.
    """
    args, arg, depth, i, n = [], "", 0, start, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            arg += text[i:j + 1]
            i = j + 1
            continue
        if c in "([":
            depth += 1
        elif c in ")]":
            if depth == 0:
                break
            depth -= 1
        elif c == "," and depth == 0:
            args.append(arg)
            arg = ""
            i += 1
            continue
        arg += c
        i += 1
    args.append(arg)
    return args


def expected_goldens(script: Path, width: int) -> int:
    """How many golden images the script can mark at a screen @a width wide.

    From the shots.take() calls in the script (tests/e2e/lib/shots.lua): with `true` it registers at
    every resolution, with `"small"` only up to 1280 pixels wide, without a flag never. A flag this
    function does not recognise counts as a golden image, so an odd script is run rather than skipped.
    """
    text = script.read_text(encoding="utf-8")
    if "golden.txt" in text:      # registered by hand, not through shots.take(): assume the worst
        return 1
    total = 0
    for m in re.finditer(r"shots\.take\s*\(", text):
        args = _take_args(text, m.end())
        if len(args) < 2:                       # shots.take(name): never a golden image
            continue
        flag = args[1].strip()
        if flag == '"small"':
            total += width <= 1280
        else:                                   # `true`, or something this does not know about
            total += 1
    return total


def png_size(path: Path):
    """(width, height) from a PNG's IHDR, without decoding anything."""
    head = path.read_bytes()[:24]
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        raise ValueError(f"{path} is not a PNG")
    return struct.unpack(">II", head[16:24])


def read_png(path: Path):
    """Decode an 8-bit RGB/RGBA non-interlaced PNG -> (w, h, channels, rows).

    `rows` holds one bytes-like object per image row. Pillow is used when importable (it decodes
    the same image ~50x faster than the pure-Python unfilter below); the stdlib decoder is the
    fallback and keeps the harness runnable without it.
    """
    if _Image is not None:
        with _Image.open(path) as im:
            w, h = im.size
            if im.mode not in ("RGB", "RGBA"):
                raise ValueError(f"{path}: unsupported PNG (mode {im.mode})")
            ch = 3 if im.mode == "RGB" else 4
            raw = im.tobytes()
        stride = w * ch
        return w, h, ch, [raw[i:i + stride] for i in range(0, len(raw), stride)]
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
    # The usual case is that the shot is the golden image bit for bit: the same build with the same
    # encoder writes the same bytes. Decoding those costs seconds per widescreen PNG (the pure-Python
    # unfilter runs over every pixel of both images), so settle it before reading a single one.
    if a.read_bytes() == b.read_bytes():
        w, h = png_size(a)
        return 0, w * h
    wa, ha, ca, ra = read_png(a)
    wb, hb, cb, rb = read_png(b)
    if (wa, ha) != (wb, hb):
        return None
    # The main menu prints the build's version string in the bottom-left corner: don't compare it.
    mask_x = 260 if "main_menu" in a.name else 0
    if _np is not None:
        return _compare_numpy(wa, ha, ca, ra, cb, rb, tol, mask_x)
    bad = 0
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


def _compare_numpy(w, h, ca, ra, cb, rb, tol, mask_x):
    """Per-pixel compare of two decoded images, vectorised with numpy.

    Matches the stdlib loop in compare(): R/G/B only (alpha is ignored) and a channel counts as
    different only when it is off by more than @a tol. @a mask_x blanks the bottom 16 rows up to
    that x (the main-menu version string).
    """
    a = _np.frombuffer(b"".join(ra), dtype=_np.uint8).reshape(h, w, ca)[..., :3]
    b = _np.frombuffer(b"".join(rb), dtype=_np.uint8).reshape(h, w, cb)[..., :3]
    bad = (_np.abs(a.astype(_np.int16) - b.astype(_np.int16)) > tol).any(axis=2)
    if mask_x:
        bad[max(h - 16, 0):, :mask_x] = False
    return int(bad.sum()), w * h


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("res", help="e.g. 1280x720")
    ap.add_argument("--update", action="store_true", default=bool(os.environ.get("JA2_UPDATE_GOLDEN")))
    ap.add_argument("--force", action="store_true",
                    help="with --update: run the tour even when it cannot register a golden image here")
    ap.add_argument("--tolerance", type=int, default=8)
    ap.add_argument("--max-diff-pct", type=float, default=0.05)
    ap.add_argument("--out", help="keep screenshots here (default: temp dir)")
    opts = ap.parse_args()

    script = Path(opts.script).resolve()
    name = script.stem
    gold_dir = GOLDEN / opts.res / name
    if opts.update and not opts.force and expected_goldens(script, int(opts.res.split("x")[0])) == 0:
        # Nothing to write: shots.take(..., "small") registers only up to 1280 pixels wide, and some
        # scripts never register one. Running the tour anyway costs minutes and produces no image.
        print(f"{name} @ {opts.res}: skipped in --update mode (this tour registers no golden image at {opts.res})")
        return 0
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
