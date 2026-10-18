#!/usr/bin/env python3
"""Which e2e tests a change affects, and how long the suite took.

`python tools/dev.py e2e --changed[=REF]` runs the e2e tests a diff can break,
plus the `smoke` tier, instead of all 59. This module owns the half of that
which cannot live in CMake - the paths of this repository - and reads the half
that must not be written down twice - which test carries which label - out of
CTest itself with `ctest --show-only=json-v1`.

The split is deliberate (issue #169). `CMakeLists.txt` declares each test's
area labels next to where the test is registered; that stays the one place a
test's identity is decided. Here we only say which *areas* a changed path
belongs to, and `dev.py` joins the two through CTest. A drift guard in
`tools/tests/test_e2e_select.py` fails when the two vocabularies separate, so
neither side can rot silently.

Standalone, for inspecting a selection without running anything:

    python tools/e2e_select.py --changed [--base REF] [--build-dir DIR]
    python tools/e2e_select.py --report _bin/e2e-timings.xml
"""

import json
import re
import shlex
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import namedtuple
from pathlib import Path

# The selector's vocabulary. These are CTest labels, not issue labels: they name
# the areas `ctest -L <area>` can select. `campaign` and `equipment` do not
# appear in the issue's area labels; they are added because the strategic and
# equipment systems are live plan areas with no screen of their own, and folding
# them into `mapscreen`/`tactical` would make those selectors match tests they
# have nothing to do with (research on #169, §5).
AREAS = (
    "tactical",
    "mapscreen",
    "laptop",
    "campaign",
    "equipment",
    "nativeui",
    "world-renderer",
    "automation",
)

# The fast tier: boot, main menu, new game to tactical, one native-UI tour, all
# at 1280x720 (the floor since #324). Always part of a `--changed` run. The CMake
# smoke pass must carry exactly these; the drift guard checks that.
SMOKE = ("main_menu", "mainmenu_parity", "new_game_to_tactical", "native_ui")

# Labels that are not areas: the tier (`e2e`, `smoke`, `localisation`) and — issue
# #302 — which data a run booted from. The behavioural suite has two sides and CI
# selects on the second one (`ctest -L '^e2e-synthetic$'`), so a synthetic run is
# never read as a real one; `--changed` keeps selecting the real runs by area.
# `localisation` (#175) is the DE/RU/PL laptop layout audit: its runs are their own
# tier, deliberately outside `e2e`/`resolution` and outside the area vocabulary, so
# neither existing sweep nor `--changed` grows by them. A drift guard keeps it so.
TIER_LABELS = ("e2e", "smoke", "e2e-real", "e2e-synthetic", "localisation")
REAL_LABEL = "e2e-real"
SYNTHETIC_LABEL = "e2e-synthetic"

_ALL = tuple(AREAS)
# A single test's own script: map to that test's areas, read back from CTest, so
# a renamed, moved or brand-new script is never guessed at - it just falls back
# to the full suite until it is registered and labelled.
_SCRIPT = object()

# Ordered; the first match wins, so the specific rules come before the general
# ones. `None` on the right means "unmapped" (see areas_for_path).
PATH_RULES = (
    (r"^tests/e2e/(?P<name>[A-Za-z0-9_]+)\.(?:lua|txt|py)$", _SCRIPT),
    (r"^tests/e2e/lib/", _ALL),  # shared fixtures every tour drives
    (r"^src/game/Tactical/", ("tactical",)),
    (r"^src/game/TacticalAI/", ("tactical",)),
    (r"^src/game/Strategic/", ("mapscreen",)),
    (r"^src/game/Laptop/", ("laptop",)),
    (r"^src/game/Equipment/", ("equipment",)),
    # NativeUI holds a whole screen per file behind one prefix; the generic rule
    # last would miss the screen each belongs to, so the big ones are named
    # first. `TacticalHud.cc`, `MapScreenNative.cc` and `LaptopScreen.cc` are
    # the reason this directory is the one place a single prefix is wrong.
    (r"^src/game/NativeUI/Tactical", ("tactical", "nativeui")),
    (r"^src/game/NativeUI/MapScreen", ("mapscreen", "nativeui")),
    (r"^src/game/NativeUI/Laptop", ("laptop", "nativeui")),
    (r"^src/game/NativeUI/(?:.*Loadout|.*WeaponReadout|.*CursorModel)", ("equipment", "nativeui")),
    (r"^src/game/NativeUI/AutoResolve", ("mapscreen", "nativeui")),
    (r"^src/game/NativeUI/", ("nativeui",)),
    (r"^src/nativeui/", ("nativeui",)),  # the native-UI core (UiCore, UiGpu, ...)
    (r"^src/sgp/(?:Video[^/]*|WorldGpu\.[^/]*|WorldPipeline\.[^/]*)$", ("world-renderer",)),
    (r"^src/sgp/shaders/", ("world-renderer",)),
    (r"^src/game/TileEngine/", ("world-renderer", "tactical")),
    (r"^src/game/Automation/", ("automation",)),
    (r"^tools/(?:ja2ctl|dev)\.py$", ("automation",)),
)

# Paths that cannot affect a test run: spend no test time on them. Kept tiny on
# purpose - anything that *can* change a run (tools/ja2ctl.py, CMakeLists.txt,
# rust/**, the rest of tools/) must stay on the fallback side below.
IGNORE_RULES = (
    r"^docs/",
    r"^\.github/",
    r"\.md$",
)

Selection = namedtuple("Selection", "areas tests fallback ignored")


class SelectError(Exception):
    """A selection could not be made (git failed, ctest was not there)."""


class CTestError(SelectError):
    """`ctest --show-only=json-v1` could not be read."""


# --- paths -> areas ---------------------------------------------------------

def _matches(rules, path):
    # search, not match: the ignore list carries patterns like `.md$` that are
    # not anchored at the start, and the path rules are `^`-anchored anyway.
    return any(re.search(rule, path) for rule in rules)


def script_areas(name, test_areas):
    """The areas of the test(s) a script drives, or None if it drives none yet.

    `check_determinism.py` drives `e2e_determinism`, so the `check_` prefix is
    tried and then dropped. A script compiled into several tests
    (`tactical_layers.lua` -> `e2e_tactical_layers_*`) unions them at the
    underscore boundary. A comparator like `check_resolution.py` has no `e2e_`
    test and comes back None (unmapped -> run all).
    """
    candidates = [f"e2e_{name}"]
    if name.startswith("check_"):
        candidates.append(f"e2e_{name[len('check_'):]}")
    for candidate in candidates:
        if candidate in test_areas:
            return set(test_areas[candidate])
    prefix = f"e2e_{name}_"
    derived = set()
    for test, areas in test_areas.items():
        if test.startswith(prefix):
            derived |= areas
    return derived or None


def areas_for_path(path, test_areas):
    """The areas a single path belongs to, or None when it is unmapped.

    `test_areas` maps a test name to its area labels (as `ctest_test_areas`
    returns). None is the safe direction: the caller runs the whole suite.
    """
    path = path.replace("\\", "/").lstrip("./")
    for pattern, areas in PATH_RULES:
        m = re.match(pattern, path)
        if not m:
            continue
        if areas is _SCRIPT:
            return script_areas(m.group("name"), test_areas)
        return set(areas)
    return None


def select(paths, test_areas, smoke=SMOKE):
    """(areas, tests, fallback, ignored) for a list of changed paths.

    An unmapped, non-ignored path makes `areas` and `fallback` None/True: the
    caller runs every test. Ignored paths contribute nothing. The smoke tier is
    always in `tests`.
    """
    areas = set()
    ignored = False
    for path in paths:
        normalised = path.replace("\\", "/")
        if _matches(IGNORE_RULES, normalised):
            ignored = True
            continue
        got = areas_for_path(normalised, test_areas)
        if got is None:
            return Selection(
                areas=None,
                tests=sorted(_with_smoke(test_areas, test_areas)),
                fallback=True,
                ignored=ignored,
            )
        areas |= got
    chosen = _with_smoke(test_areas, (t for t, a in test_areas.items() if a & areas))
    return Selection(areas=areas, tests=sorted(chosen), fallback=False, ignored=ignored)


def _with_smoke(test_areas, tests):
    chosen = set(tests)
    for name in SMOKE:
        test = f"e2e_{name}"
        if test in test_areas:
            chosen.add(test)
    return chosen


def test_regex(tests):
    """A `ctest -R` regex selecting exactly `tests`."""
    return "^(" + "|".join(re.escape(t) for t in sorted(tests)) + ")$"


# --- git --------------------------------------------------------------------

def changed_paths(repo, base="master"):
    """Tracked changes against `base`, plus untracked files, repo-relative.

    `git diff <base>` compares the working tree to the base commit, so it
    already includes staged and unstaged edits - which is what an agent that
    has not committed yet needs. Untracked files are added because a brand-new
    source or test file is exactly a change a selector must not miss.
    """
    def git(*args):
        try:
            r = subprocess.run(["git", "-C", str(repo), *args],
                               capture_output=True, text=True)
        except OSError as exc:  # git is not on this process's PATH
            raise SelectError(f"could not run git: {exc}")
        if r.returncode != 0:
            raise SelectError(f"git {' '.join(args)} failed: "
                              f"{(r.stderr or r.stdout).strip()}")
        return r.stdout

    tracked = git("diff", "--name-only", base)
    untracked = git("ls-files", "--others", "--exclude-standard")
    seen = {}
    for line in (tracked + "\n" + untracked).splitlines():
        line = line.strip()
        if line:
            seen.setdefault(line.replace("\\", "/"), None)
    return list(seen)


# --- ctest's own labels -----------------------------------------------------

def parse_ctest_labels(text):
    """{test name: set(labels)} for every test CTest's JSON dump describes."""
    try:
        data = json.loads(text)
    except ValueError as exc:
        raise CTestError(f"ctest --show-only=json-v1 did not emit JSON: {exc}")
    labels = {}
    for test in data.get("tests", []):
        for prop in test.get("properties", []):
            if prop.get("name") == "LABELS":
                labels[test["name"]] = set(prop.get("value", []))
    return labels


def parse_ctest_json(text):
    """{e2e test name: area labels}, the join input for `select`."""
    all_labels = parse_ctest_labels(text)
    return {name: labels - set(TIER_LABELS)
            for name, labels in all_labels.items() if "e2e" in labels}


def default_ctest_run(argv, cwd):
    """Run a command for the ctest dump; the plain form for macOS/Linux/CI.

    On Windows `ctest` lives in MSYS2, so `dev.py` passes its own `tool_run`
    instead of relying on the PATH of the Python process.
    """
    try:
        r = subprocess.run(argv, cwd=str(cwd), capture_output=True, text=True)
    except OSError as exc:
        raise CTestError(f"could not run {argv[0]}: {exc}")
    if r.returncode != 0:
        raise CTestError(f"{' '.join(argv)} failed: {(r.stderr or r.stdout).strip()}")
    return r.stdout


def ctest_test_areas(build_dir, run=default_ctest_run):
    return parse_ctest_json(run(["ctest", "--show-only=json-v1"], build_dir))


def select_for_changed(repo, build_dir, base="master", run=default_ctest_run):
    """The whole `--changed` computation: diff -> areas -> tests."""
    return select(changed_paths(repo, base), ctest_test_areas(build_dir, run=run))


# --- the slowest-test report ------------------------------------------------

Entry = namedtuple("Entry", "name seconds labels")


def parse_junit(text):
    """(name, seconds, labels) for every `testcase` in a ctest JUnit report."""
    try:
        root = ET.fromstring(text)
    except ET.ParseError as exc:
        raise SelectError(f"not a ctest JUnit file: {exc}")
    entries = []
    for case in root.iter("testcase"):
        labels = ""
        for prop in case.iter("property"):
            if prop.get("name") == "cmake_labels":
                labels = prop.get("value", "")
                break
        entries.append(Entry(case.get("name", "?"), float(case.get("time", "0") or 0), labels))
    return entries


def fmt_secs(seconds):
    return f"{int(seconds) // 60}m{int(seconds) % 60:02d}s" if seconds >= 60 else f"{seconds:.1f}s"


def report_lines(entries, count=10):
    """The slowest `count` tests, name, seconds and labels, plus the total."""
    if not entries:
        return []
    total = sum(e.seconds for e in entries)
    lines = [f"slowest {min(count, len(entries))} of {len(entries)} e2e tests "
             f"(total test time {fmt_secs(total)}):"]
    for i, e in enumerate(sorted(entries, key=lambda e: e.seconds, reverse=True)[:count], 1):
        lines.append(f"  {i:>2}. {e.name:<38} {fmt_secs(e.seconds):>8}  {e.labels}")
    return lines


# --- standalone -------------------------------------------------------------

def _main(argv):
    import argparse

    p = argparse.ArgumentParser(prog="tools/e2e_select.py", description=__doc__.splitlines()[0])
    p.add_argument("--changed", action="store_true", help="what a diff against --base selects")
    p.add_argument("--base", default="master", help="base ref for --changed (default: master)")
    p.add_argument("--build-dir", default=None, help="configured build dir (default: _bin/ or build/)")
    p.add_argument("--report", default=None, help="print the slowest tests from this JUnit file")
    p.add_argument("--count", type=int, default=10, help="how many tests --report shows")
    args = p.parse_args(argv)

    repo = Path(__file__).resolve().parent.parent
    if args.report:
        for line in report_lines(parse_junit(Path(args.report).read_text(encoding="utf-8")), args.count):
            print(line)
        return 0
    if not args.changed:
        p.error("nothing to do: pass --changed or --report")
    build_dir = Path(args.build_dir) if args.build_dir else next(
        (repo / name for name in ("_bin", "build") if (repo / name).is_dir()), repo / "_bin")
    selection = select_for_changed(repo, build_dir, base=args.base)
    if selection.fallback:
        print(f"no area maps one of the changed paths: running all {len(selection.tests)} e2e tests")
    else:
        print(f"areas: {', '.join(sorted(selection.areas)) or '(none)'}")
    print("tests: " + " ".join(selection.tests))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(_main(sys.argv[1:]))
    except SelectError as exc:
        print(f"e2e_select: error: {exc}", file=sys.stderr)
        sys.exit(1)
