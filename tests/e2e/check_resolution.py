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

With --update, a script that cannot mark a golden image at this resolution (its
shots.take() calls register none - see expected_goldens()) is skipped instead of
run: it would cost minutes of tour to write nothing. The plain comparison run
still runs it, because it still checks the layout.

The tour runs with the wall clock pinned and the version label fixed (see
GOLDEN_CLOCK / GOLDEN_VERSION below). Two things the UI shows are not a function
of the game's state: the version label carries the build's commit sha, and a
save's filename and "Today HH:MM" carry the machine's clock and timezone. Left
alone they make a golden image depend on when and where it was taken, so the
screens that show them would fail on every other machine and after every commit.

A failing tour names the layer that failed: the tour (the driver printed
"FAILED", or ended on one of its own codes without one), the golden compare (this
file's exit code 5) or the process (no verdict, or a code outside the driver's
1-4: a kill or a crash, never the script's result even when the code suggests
one). The tour's own "passed"/"FAILED" line decides which. Code 5 also carries a
script whose shots.take() calls register a golden image but which marks none: the
registration is unreachable, so the tour compares nothing while reading as pixel
coverage (issue #389).
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

# The driver's exit-code vocabulary lives in ja2ctl (it is the program that runs the game), so
# the harness reads it from there rather than keeping a second copy that could drift.
sys.path.insert(0, str(JA2CTL.parent))
import ja2ctl  # noqa: E402

# This harness's own failure code for a golden compare that failed. The driver never exits with
# it (ja2ctl.EXIT_MEANINGS is 0-4), so the three ways a tour can fail - the script, the compare,
# the process - are each a different code and a different message (issue #45).
EXIT_GOLDEN_MISMATCH = 5

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

    The count is only honest if every such call is reachable. A registration the tour returns before
    - a shots.take(..., true) after an early `return` - makes this promise more than the script
    delivers, and --update then runs the tour to write nothing; main() fails such a script rather
    than report it (issue #389).
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


# Nothing is masked. Whatever a shot shows has to be the same on every machine, so the
# non-deterministic parts of the UI (the version label, the wall clock) are pinned at the
# source instead — see GOLDEN_CLOCK / GOLDEN_VERSION. An earlier version of this file masked
# the main menu's bottom-left corner for the version string; it never fired (the string sits
# above the last 16 rows, and the test it keyed on did not match the shot's name), which is
# how three goldens could drift for so long unnoticed.


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
    return int(bad.sum()), wa * ha


# The tour's own verdict, printed by the game (Automation.cc: "automation: <script> passed (N
# frames, M ms game time)" / "automation: <script> FAILED ..."). It is the only statement about
# whether the tour itself succeeded, and it is what tells a failure in the script from a process
# that died after the script was done.
VERDICT = re.compile(r"^automation: .*? (passed|FAILED)\b")


def failure_reason(code: int, verdict, goldens_marked: int) -> str:
    """Name which layer failed: the script, the golden compare, or the process.

    A nonzero exit used to print "script failed with exit code N" whatever the code was, which is
    wrong twice over: the code need not be the driver's own (a killed process reports a code the
    driver never exits with - 0xffffffff, or 1 for Windows' taskkill /F - and the raw number reads
    as a crash), and it never said whether the golden compare had even run. The tour's own verdict
    line decides, because the driver prints "passed" or "FAILED" for every result the script
    produced; the code only says what the process did. The compare's own failure has its own
    message below; this covers the other two (issue #45).
    """
    if verdict == "passed":
        return (f"the script passed and the process died after it (exit code {code}: "
                f"{ja2ctl.describe_exit_code(code)}); {goldens_marked} golden image(s) marked, "
                "none compared")
    if verdict == "FAILED":
        return (f"the script failed (exit code {code}: "
                f"{ja2ctl.EXIT_MEANINGS.get(code, ja2ctl.describe_exit_code(code))}); "
                f"{goldens_marked} golden image(s) marked")
    if code == 1:
        # Automation.cc prints a FAILED line before every exit 1, so an exit 1 with no verdict is
        # not the driver's own result: the process died, and a force-kill can use any code.
        return ("the process died before the script reached its verdict (exit code 1 with no FAILED "
                "line: the driver prints one before it exits 1, so this is a kill or a crash - "
                "Windows' taskkill /F uses code 1 - not the script); no golden image compared")
    if code in ja2ctl.EXIT_MEANINGS:
        return (f"the tour ended before a verdict (exit code {code}: {ja2ctl.EXIT_MEANINGS[code]}); "
                "no golden image compared")
    return (f"the process died before the script reached its verdict (exit code {code}: "
            f"{ja2ctl.describe_exit_code(code)}); no golden image compared")


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
        # Echo the game's output as it arrives and keep the tour's own verdict line: the exit code
        # alone cannot say whether the tour was the failure, and for a process that was killed it
        # is a number the driver never emits (see failure_reason).
        verdict = None
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for raw in proc.stdout:
            line = raw.decode("utf-8", errors="replace")
            sys.stdout.write(line)
            sys.stdout.flush()
            m = VERDICT.match(line.strip())
            if m:
                verdict = m.group(1)
        code = proc.wait()
        marked = out / "golden.txt"
        golden_shots = marked.read_text().split() if marked.exists() else []
        expected = expected_goldens(script, int(opts.res.split("x")[0]))
        if code != 0:
            print(f"{name} @ {opts.res}: {failure_reason(code, verdict, len(golden_shots))}",
                  file=sys.stderr)
            # The raw code is in the message; the process exit code has to be one exit() can carry
            # (a killed Windows process reports 0xffffffff, which raises instead).
            return ja2ctl.exit_status(code)
        if not golden_shots:
            if expected:
                # The script's shots.take() calls register golden image(s) here, but the tour
                # marked none: the registration is unreachable (an early return, a branch it no
                # longer takes), so the test reads as pixel coverage it does not have - and an
                # --update run would drive it to write nothing. Say so instead of passing a test
                # that cannot fail (issue #389).
                print(f"{name} @ {opts.res}: the tour marked no golden image, but its shots.take() "
                      f"calls register {expected} at {opts.res}: the registration is unreachable "
                      "(an early return, a branch the tour no longer takes). Remove the "
                      "registration, or run it and mark it - do not leave a test that cannot fail",
                      file=sys.stderr)
                return EXIT_GOLDEN_MISMATCH
            print(f"{name} @ {opts.res}: passed (compares no golden image at this resolution)")
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
            print(f"{name} @ {opts.res}: the golden compare failed, the script did not:\n  "
                  + "\n  ".join(failed), file=sys.stderr)
            return EXIT_GOLDEN_MISMATCH
        print(f"{name} @ {opts.res}: passed ({len(golden_shots)} golden image(s) match)")
        return 0
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
