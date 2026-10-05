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
PNGs with Pillow and compares them per pixel with numpy. Both are project
dependencies managed by uv: run `uv sync` once in the repo root and a fresh
cmake configure picks `.venv` up for ctest (see COMPILATION.md).

With --update, a script that cannot mark a golden image at this resolution
marks none, see expected_goldens()) is skipped instead of run: it would cost
minutes of tour to write nothing. The plain comparison run still runs it,
because it still checks the layout.

The tour runs with the wall clock pinned and the version label fixed (see
GOLDEN_CLOCK / GOLDEN_VERSION below). Two things the UI shows are not a function
of the game's state: the version label carries the build's commit sha, and a
save's filename and "Today HH:MM" carry the machine's clock and timezone. Left
alone they make a golden image depend on when and where it was taken, so the
screens that show them would fail on every other machine and after every commit.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

# Project dependencies, installed into .venv by `uv sync` (pyproject.toml/uv.lock). ctest uses that
# interpreter; a direct run needs it too, so fail with the fix instead of a bare ImportError.
try:
    import numpy as _np
    from PIL import Image as _Image
except ImportError as exc:
    sys.exit(f"check_resolution: {exc.name} is missing; run `uv sync` in the repo root first")

HERE = Path(__file__).resolve().parent
JA2CTL = HERE.parent.parent / "tools" / "ja2ctl.py"
GOLDEN = HERE / "golden"

# What the tour runs with so a golden image is a function of the game's state alone. Both are
# passed to every run, in --update mode too, so a regenerated golden carries the same fixed values
# as the ones it replaces and stays comparable across machines.
#
# GOLDEN_CLOCK is a UTC instant: it names a save's filename and the "Today HH:MM" beside it, and
# the game renders a frozen instant in UTC (see sgp::Clock::IsWallFrozen), so the machine's
# timezone cannot change what it prints. 2026-01-15T12:00:00Z is an arbitrary but fixed choice.
GOLDEN_CLOCK = "2026-01-15T12:00:00Z"
# Without this the label is the build's `git rev-parse --short HEAD`, which would make the main
# menu, the message box and every other screen showing it differ on every commit.
GOLDEN_VERSION = "Pecel Bakwan golden"


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
    """Decode an 8-bit RGB/RGBA PNG -> (w, h, numpy array)."""
    with _Image.open(path) as im:
        if im.mode not in ("RGB", "RGBA"):
            raise ValueError(f"{path}: unsupported PNG (mode {im.mode})")
        return im.size[0], im.size[1], _np.asarray(im)


def compare(a: Path, b: Path, tol: int):
    """Returns (differing_pixels, total_pixels) or None if sizes differ."""
    # The usual case is that the shot is the golden image bit for bit: the same build with the same
    # encoder writes the same bytes. Decoding those costs the most on a mismatching widescreen PNG,
    # so settle it before reading a single one.
    if a.read_bytes() == b.read_bytes():
        w, h = png_size(a)
        return 0, w * h
    wa, ha, ra = read_png(a)
    wb, hb, rb = read_png(b)
    if (wa, ha) != (wb, hb):
        return None
    # R/G/B only (alpha is ignored); a channel counts as different only when off by more than tol.
    bad = (_np.abs(ra[..., :3].astype(_np.int16) - rb[..., :3].astype(_np.int16)) > tol).any(axis=2)
    # The main menu prints the build's version string in the bottom-left corner: don't compare it.
    mask_x = 260 if "main_menu" in a.name else 0
    if mask_x:
        bad[max(ha - 16, 0):, :mask_x] = False
    return int(bad.sum()), wa * ha


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
               "--res", opts.res, "--timeout", "2400", "--out", str(out), "--arg", "golden=1",
               "--freeze-wall-clock", GOLDEN_CLOCK, "--version-label", GOLDEN_VERSION]
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
