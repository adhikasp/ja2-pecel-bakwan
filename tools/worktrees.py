#!/usr/bin/env python3
"""worktrees.py - list this repo's worktrees and garbage-collect the dead ones.

Every agent works in its own worktree and every worktree carries its own build
directory, so a machine accumulates tens of worktrees and tens of gigabytes
that nobody ever removes. `git worktree list` knows they are there, but it says
nothing about whether the work in them is still needed.

    python tools/dev.py worktrees                 # the table
    python tools/dev.py worktrees --sizes         # measure the trees (slow, cached)
    python tools/dev.py worktrees --prune --dry-run
    python tools/dev.py worktrees --prune         # remove the finished ones

A worktree is **prunable** only when removing it cannot lose work:

  * its HEAD is already an ancestor of `origin/master`, so the work is merged,
  * it has no uncommitted or untracked changes,
  * no PR is open on its branch,
  * no build of ours is running in it,
  * it is not locked, not the main checkout and not the worktree you stand in.

A detached HEAD follows the same rule: a branch that was deleted while its
commits never reached master is *unmerged work*, and it stays - listed as such,
because deleting the directory would be the one way to lose it for good. A
worktree whose directory is already gone is always cleaned up: there is nothing
left to lose, only git's stale administrative record.

Sizes are measured by walking each tree, which costs seconds per gigabyte -
far too slow to do for fifty worktrees on every run - so they are opt-in
(`--sizes`) and cached in the machine state directory. The prune confirmation
always measures what it is about to delete, because that is the number the
decision is about.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

DEFAULT_SLUG = "adhikasp/ja2-pecel-bakwan"

# Measured sizes go stale as agents build in their trees; six hours is long
# enough that a `--prune` run finds them fresh and short enough that a
# yesterday's number is never what an agent reads as "what this costs now".
SIZE_CACHE_TTL = 6 * 3600

BUILD_DIR_NAMES = ("_bin", "build")  # Windows and everywhere else (see dev.py)

# Marker dev.py's build_lock() drops in the build directory while ninja runs.
BUILD_LOCK = ".dev-build.lock"


class GitError(RuntimeError):
    """A git command failed; the message carries git's own stderr."""


def git(*args: str, cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess:
    argv = ["git", *args]
    proc = subprocess.run(argv, cwd=str(cwd) if cwd else None, capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise GitError(f"git {' '.join(args)}: {(proc.stderr or proc.stdout).strip()}")
    return proc


# --- the facts, in one row per worktree --------------------------------------

@dataclass
class Row:
    path: str
    head: str = ""
    branch: str | None = None
    bare: bool = False
    locked: bool = False
    missing_dir: bool = False       # git says prunable: the directory is gone
    exists: bool = True
    merged: bool = False
    branch_exists: bool = True
    age: float | None = None        # seconds since the HEAD commit
    pr: dict | None = None
    dirty: bool | None = None
    size: int | None = None
    busy: bool = False              # one of our builds holds the lock
    main: bool = False
    here: bool = False

    @property
    def name(self) -> str:
        return self.branch or "(detached)"

    @property
    def pr_state(self) -> str:
        return ((self.pr or {}).get("state") or "").upper()

    @property
    def in_master(self) -> bool:
        """Whether this worktree's work has reached master already.

        Ancestry is not the whole answer in this repository: pull requests are
        squash merged, so the head of a merged PR's branch is never an ancestor
        of master - the merge commit is a different one. The pull request is the
        evidence that its work is in, and it stays on record after the branch
        and the worktree are gone.
        """
        return self.merged or self.pr_state == "MERGED"

    def as_json(self) -> dict:
        blocker = prune_blocker(self)
        return {
            "path": self.path,
            "branch": self.branch,
            "detached": self.branch is None,
            "head": self.head,
            "merged": self.merged,
            "in_master": self.in_master,
            "branch_exists": self.branch_exists,
            "dirty": self.dirty,
            "size": self.size,
            "age_days": None if self.age is None else round(self.age / 86400, 2),
            "pr": None if self.pr is None else {
                "number": self.pr.get("number"), "state": self.pr.get("state"), "url": self.pr.get("url")},
            "main": self.main,
            "here": self.here,
            "prunable": blocker is None,
            "blocked_because": blocker,
        }


def parse_worktrees(text: str) -> list[Row]:
    """`git worktree list --porcelain` -> rows. Blank line separates worktrees.

    Parsed rather than read off `git worktree list` prose so a path with a
    space in it (every agent worktree on Windows lives under a user profile
    with one) cannot split the row in two.
    """
    rows: list[Row] = []
    current: Row | None = None
    for raw in text.splitlines():
        line = raw.rstrip("\n")
        if not line.strip():
            if current is not None:
                rows.append(current)
            current = None
            continue
        if current is None:
            current = Row(path="")
        key, _, value = line.partition(" ")
        if key == "worktree":
            current.path = value
        elif key == "HEAD":
            current.head = value
        elif key == "branch":
            current.branch = value[len("refs/heads/"):] if value.startswith("refs/heads/") else value
        elif key == "detached":
            current.branch = None
        elif key == "bare":
            current.bare = True
        elif key == "locked":
            current.locked = True
        elif key == "prunable":
            current.missing_dir = True
    if current is not None:
        rows.append(current)
    return rows


# --- formatting --------------------------------------------------------------

def format_size(num: int | None) -> str:
    if num is None:
        return "-"
    for unit, step in (("TB", 1 << 40), ("GB", 1 << 30), ("MB", 1 << 20), ("kB", 1 << 10)):
        if num >= step:
            return f"{num / step:.1f} {unit}"
    return f"{num} B"


def format_age(seconds: float | None) -> str:
    if seconds is None:
        return "-"
    if seconds < 90:
        return "now"
    if seconds < 3600:
        return f"{int(seconds // 60)}m"
    if seconds < 86400:
        return f"{int(seconds // 3600)}h"
    if seconds < 365 * 86400:
        return f"{int(seconds // 86400)}d"
    return f"{seconds / (365 * 86400):.1f}y"


def normalize(path: str | Path) -> str:
    """A comparable form of a path: same value for the same directory."""
    try:
        return os.path.normcase(os.path.realpath(str(path)))
    except OSError:
        return os.path.normcase(str(path))


def shorten(path: str, main_path: str, home: str) -> str:
    """The shortest unambiguous form: relative to the checkout, or `~`."""
    norm = normalize(path)
    for root, prefix in ((normalize(main_path), ""), (normalize(home), "~/")):
        if root and norm.startswith(root.rstrip("/\\") + os.sep):
            return prefix + path[len(root.rstrip("/\\")) + 1:]
    return path


def state_of(row: Row) -> str:
    if row.main:
        base = "main"
    elif row.here:
        base = "here"
    elif not row.exists:
        base = "missing"
    elif row.in_master:
        base = "merged"
    else:
        base = "unmerged"
    flags = []
    if not row.branch_exists and row.branch:
        flags.append("branch gone")
    if row.locked:
        flags.append("locked")
    if row.busy:
        flags.append("building")
    if row.dirty:
        flags.append("dirty")
    return ", ".join([base, *flags])


def pr_of(row: Row) -> str:
    if not row.pr:
        return "-"
    state = (row.pr.get("state") or "").upper()
    word = {"OPEN": "open", "MERGED": "merged", "CLOSED": "closed"}.get(state, state.lower())
    return f"#{row.pr.get('number')} {word}"


def prune_blocker(row: Row) -> str | None:
    """Why this row must not be removed, or None when it is safe to remove.

    Ordered cheapest-and-most-dangerous first, and every answer is a reason a
    human would also give: this is the list that must never contain a tree
    somebody's work is still in.
    """
    if row.main:
        return "the main checkout"
    if row.here:
        return "this worktree"
    if not row.exists:
        return None  # nothing left on disk; only git's record to drop
    if row.locked:
        return "locked"
    if row.busy:
        return "a build is running in it"
    if row.pr_state == "OPEN":
        return f"PR #{row.pr.get('number')} is open"
    if row.dirty:
        return "uncommitted changes"
    if not row.in_master:
        return "unmerged work"
    return None


def render(rows: list[Row], main_path: str, home: str) -> str:
    """The table. Prunable rows first: those are the ones worth acting on."""
    header = ("SIZE", "AGE", "STATE", "PR", "BRANCH", "HEAD", "PATH")
    body = [
        (format_size(row.size), format_age(row.age), state_of(row), pr_of(row),
         row.name, row.head[:8], shorten(row.path, main_path, home))
        for row in sorted(rows, key=lambda r: (prune_blocker(r) is not None, normalize(r.path)))
    ]
    widths = [max(len(header[i]), max((len(b[i]) for b in body), default=0)) for i in range(7)]
    lines = ["  ".join(c.ljust(w) for c, w in zip(header[:6], widths[:6])).rstrip()]
    for entry in body:
        cells = [entry[0].rjust(widths[0]), entry[1].rjust(widths[1]),
                 *[entry[i].ljust(widths[i]) for i in range(2, 6)], entry[6]]
        lines.append("  ".join(cells).rstrip())
    return "\n".join(lines)


# --- sizes -------------------------------------------------------------------

def dir_size(path: str) -> int:
    """Bytes under `path`, following no symlinks (a worktree's `.git` is a file)."""
    total = 0
    stack = [path]
    while stack:
        current = stack.pop()
        try:
            with os.scandir(current) as entries:
                for entry in entries:
                    try:
                        if entry.is_dir(follow_symlinks=False):
                            stack.append(entry.path)
                        else:
                            total += entry.stat(follow_symlinks=False).st_size
                    except OSError:
                        continue
        except OSError:
            continue
    return total


def measure(paths: list[str], jobs: int = 8) -> dict[str, int]:
    if not paths:
        return {}
    with ThreadPoolExecutor(max_workers=max(1, min(jobs, len(paths)))) as pool:
        return dict(zip(paths, pool.map(dir_size, paths)))


def note(message: str) -> None:
    """Progress and warnings, on stderr: stdout carries the table or the JSON."""
    print(message, file=sys.stderr, flush=True)


def load_size_cache(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_size_cache(path: Path, data: dict) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=1, sort_keys=True), encoding="utf-8")
    except OSError:
        pass  # a cache is an optimisation, never a reason to fail the command


# --- the GitHub side ---------------------------------------------------------

def pr_index(slug: str, limit: int = 500) -> dict[str, dict]:
    """Latest PR per head branch, in one API call: `gh` once beats 50 calls."""
    if not shutil.which("gh"):
        return {}
    proc = subprocess.run(
        ["gh", "pr", "list", "-R", slug, "--state", "all", "--limit", str(limit),
         "--json", "number,state,headRefName,url"],
        capture_output=True, text=True)
    if proc.returncode != 0:
        return {}
    try:
        items = json.loads(proc.stdout)
    except ValueError:
        return {}
    index: dict[str, dict] = {}
    for pr in items:
        head = pr.get("headRefName")
        if head and (head not in index or pr.get("number", 0) > index[head].get("number", 0)):
            index[head] = pr
    return index


# --- gathering ---------------------------------------------------------------

def resolve_base(repo: Path) -> str | None:
    for ref in ("refs/remotes/origin/master", "refs/heads/master"):
        if git("show-ref", "--verify", "--quiet", ref, cwd=repo, check=False).returncode == 0:
            return ref
    return None


def is_build_running(path: str) -> bool:
    """Whether one of our own builds holds this worktree's build lock."""
    for name in BUILD_DIR_NAMES:
        lock = Path(path) / name / BUILD_LOCK
        try:
            if lock.exists() and time.time() - lock.stat().st_mtime < 6 * 3600:
                return True
        except OSError:
            continue
    return False


def branches_containing(repo: Path, sha: str) -> set[str]:
    """Local branches whose history contains `sha` (one call per commit)."""
    out = git("for-each-ref", f"--contains={sha}", "--format=%(refname:short)", "refs/heads",
              cwd=repo).stdout
    return set(out.split())


def collect(repo: Path, *, here: Path | None = None, fetch: bool = True, slug: str = DEFAULT_SLUG,
            with_prs: bool = True, prs: dict[str, dict] | None = None,
            with_dirty: bool = False, jobs: int = 8) -> tuple[list[Row], str | None]:
    """One row per worktree, with everything the table and the prune plan need."""
    if fetch:
        git("fetch", "--quiet", "--no-tags", cwd=repo, check=False)
    base = resolve_base(repo)
    rows = parse_worktrees(git("worktree", "list", "--porcelain", cwd=repo).stdout)
    if not rows:
        return [], base
    rows[0].main = True  # git always lists the main worktree first

    if prs is None:
        prs = pr_index(slug) if with_prs else {}
    here_norm = normalize(here) if here else None

    local_branches: set[str] = set()
    merged: set[str] = set()
    if base:
        local_branches = set(git("for-each-ref", "--format=%(refname:short)", "refs/heads",
                                 cwd=repo).stdout.split())
        merged = set(git("for-each-ref", f"--merged={base}", "--format=%(refname:short)", "refs/heads",
                         cwd=repo).stdout.split())

    # Commit dates for every worktree head in one `git log --no-walk` call.
    ages: dict[str, float] = {}
    heads = {row.head for row in rows if row.head}
    if heads:
        out = git("log", "--no-walk=unsorted", "--format=%H %ct", *sorted(heads), cwd=repo).stdout
        for line in out.splitlines():
            sha, _, stamp = line.partition(" ")
            if sha and stamp.strip().isdigit():
                ages[sha] = time.time() - int(stamp.strip())

    for row in rows:
        row.exists = (not row.bare) and Path(row.path).exists()
        row.missing_dir = row.missing_dir or not row.exists
        row.here = here_norm is not None and normalize(row.path) == here_norm
        row.age = ages.get(row.head)
        row.pr = prs.get(row.branch) if row.branch else None
        if row.branch:
            row.branch_exists = row.branch in local_branches
            row.merged = row.branch in merged
        elif base and row.head:
            row.branch_exists = False
            if git("merge-base", "--is-ancestor", row.head, base, cwd=repo,
                   check=False).returncode == 0:
                row.merged = True
            else:
                # A detached head has no branch to look a pull request up by, so
                # ask the other way round: some branch that holds this commit
                # was merged, so its content is on master. Asking which branch
                # it was is cheap - there are only ever a few detached trees.
                row.merged = any((prs.get(name) or {}).get("state") == "MERGED"
                                 for name in branches_containing(repo, row.head))
        row.busy = row.exists and is_build_running(row.path)

    if with_dirty:
        def check_dirty(row: Row) -> bool:
            if not row.exists:
                return False
            proc = git("status", "--porcelain", cwd=row.path, check=False)
            return bool(proc.stdout.strip())

        with ThreadPoolExecutor(max_workers=max(1, min(jobs, len(rows)))) as pool:
            for row, dirty in zip(rows, pool.map(check_dirty, rows)):
                row.dirty = dirty
    return rows, base


# --- pruning -----------------------------------------------------------------

def plan_prune(rows: list[Row]) -> list[Row]:
    """The rows `--prune` would remove, with the dirty check filled in.

    Dirtiness is only worth asking about for a row everything else has already
    cleared, so it is the last gate - and never a gate that is skipped.
    """
    doomed = [row for row in rows if prune_blocker(row) is None]
    todo = [row for row in doomed if row.exists]
    if todo:
        def check_dirty(row: Row) -> bool:
            proc = git("status", "--porcelain", cwd=row.path, check=False)
            return bool(proc.stdout.strip())

        with ThreadPoolExecutor(max_workers=8) as pool:
            for row, dirty in zip(todo, pool.map(check_dirty, todo)):
                row.dirty = dirty
    return [row for row in doomed if prune_blocker(row) is None]


def prune(repo: Path, rows: list[Row], log=print, *, dry_run: bool = False, yes: bool = False,
          keep_branches: bool = False, home: str | None = None, say=print) -> int:
    """Remove the worktrees `prune_blocker` cleared, then their merged branches."""
    home = home or str(Path.home())
    doomed = plan_prune(rows)
    if not doomed:
        log("nothing to prune: every worktree still holds work, or is in use")
        return 0
    real = [row for row in doomed if row.exists]
    gone = [row for row in doomed if not row.exists]
    freed = measure([row.path for row in real])

    say(f"About to remove {len(doomed)} worktree(s)"
        + (f" ({len(gone)} already gone)" if gone else "")
        + f", freeing {format_size(sum(freed.values()))} of build directories:")
    for row in sorted(real, key=lambda r: -freed[r.path]):
        say(f"  {format_size(freed[row.path]):>8}  {row.head[:8]}  {row.name:<40}"
            f"  {shorten(row.path, repo, home)}")
    for row in gone:
        say(f"  {'(gone)':>8}  {row.head[:8]}  {row.name:<40}  {shorten(row.path, repo, home)}"
            "  - only the git record")

    if dry_run:
        say("dry run: nothing was removed")
        return 0
    if not yes:
        if not sys.stdin or not sys.stdin.isatty():
            say("stdin is not a terminal: re-run with --yes to confirm, or --dry-run to just look.")
            return 2
        answer = input("This deletes those directories, build directories and all. Type 'yes' to continue: ")
        if answer.strip().lower() not in ("y", "yes"):
            say("cancelled")
            return 1

    removed: list[Row] = []
    failed: list[tuple[Row, str]] = []
    for row in real:
        proc = git("worktree", "remove", row.path, cwd=repo, check=False)
        if proc.returncode == 0:
            removed.append(row)
        else:
            failed.append((row, (proc.stderr or proc.stdout).strip()))
    for row, err in failed:
        log(f"could not remove {shorten(row.path, repo, home)}: {err.splitlines()[-1] if err else 'git said no'}")

    git("worktree", "prune", cwd=repo, check=False)  # drop the records of gone trees
    log(f"removed {len(removed)} worktree(s)"
        + (f" and {len(gone)} stale record(s)" if gone else "")
        + (f", {len(failed)} left alone" if failed else ""))

    if keep_branches:
        return 0
    # `-d` with master as the merge base, so git - not this script - has the
    # last word on whether a branch is merged. A branch checked out in another
    # worktree, or unmerged, is kept and reported.
    base = resolve_base(repo)
    for row in sorted(removed, key=lambda r: r.name):
        if not row.branch:
            continue
        proc = git("branch", "-d", row.branch, *([base] if base else []), cwd=repo, check=False)
        message = (proc.stderr or proc.stdout).strip().splitlines()
        if proc.returncode != 0:
            log(f"kept branch {row.branch}: {message[-1] if message else 'git refused'}")
        else:
            log(f"deleted branch {row.branch}")
    return 0


# --- the command -------------------------------------------------------------

def add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--sizes", action="store_true",
                        help="measure every worktree (walks the trees: slow, cached for later runs)")
    parser.add_argument("--prune", action="store_true",
                        help="remove the worktrees whose branch is merged or already gone (asks first)")
    parser.add_argument("--dry-run", action="store_true", help="with --prune: show the plan, remove nothing")
    parser.add_argument("--yes", action="store_true", help="with --prune: skip the confirmation")
    parser.add_argument("--keep-branches", action="store_true",
                        help="with --prune: remove the worktrees but keep their local branches")
    parser.add_argument("--no-fetch", action="store_true",
                        help="do not fetch; decide 'merged' against the origin/master we already have")
    parser.add_argument("--no-pr", action="store_true", help="skip the pull requests (one `gh` call)")
    parser.add_argument("--dirty", action="store_true",
                        help="also ask every worktree for uncommitted work (one `git status` each; --prune "
                             "always does this for the rows it considers)")
    parser.add_argument("--json", action="store_true", help="machine-readable rows instead of the table")
    parser.add_argument("--slug", default=DEFAULT_SLUG, help="repository the pull requests come from")


def main(argv: list[str] | None = None, *, repo: Path, here: Path | None = None,
         state_dir: Path | None = None, log=print) -> int:
    parser = argparse.ArgumentParser(
        prog="dev.py worktrees", description="list and garbage-collect this repo's worktrees")
    add_arguments(parser)
    args = parser.parse_args(argv)

    try:
        rows, base = collect(repo, here=here, fetch=not args.no_fetch, slug=args.slug,
                             with_prs=not args.no_pr, with_dirty=args.dirty)
    except GitError as exc:
        note(f"error: {exc}")
        return 1
    if not rows:
        note("no worktrees (is this a git repository?)")
        return 1
    if base is None:
        note("warning: no origin/master and no local master; nothing can be called merged")

    cache_file = (state_dir / "worktree-sizes.json") if state_dir else None
    cache = load_size_cache(cache_file) if cache_file else {}
    # Sizes already measured, and measured recently enough to stand for now.
    cached = {row.path: int(cache[row.path]["bytes"])
              for row in rows
              if row.exists and isinstance(cache.get(row.path), dict)
              and time.time() - float(cache[row.path].get("at", 0)) < SIZE_CACHE_TTL}
    for row in rows:
        row.size = cached.get(row.path)

    if args.sizes:
        targets = [row for row in rows if row.exists and row.path not in cached]
        note(f"measuring {len(targets)} worktree(s) ...")
        fresh = measure([row.path for row in targets])
        cache.update({path: {"bytes": size, "at": time.time()} for path, size in fresh.items()})
        if cache_file:
            save_size_cache(cache_file, cache)
        for row in rows:
            row.size = cached.get(row.path, fresh.get(row.path))

    if args.prune:
        return prune(repo, rows, log, dry_run=args.dry_run, yes=args.yes,
                     keep_branches=args.keep_branches)

    if args.json:
        print(json.dumps([row.as_json() for row in rows], indent=1))
        return 0

    print(render(rows, rows[0].path, str(Path.home())))
    prunable = [row for row in rows if prune_blocker(row) is None]
    known = sum(1 for row in rows if row.size is not None)
    total = sum(row.size for row in rows if row.size is not None)
    print()
    print(f"{len(rows)} worktrees, {len(prunable)} prunable"
          + (f", {format_size(total)} measured over {known} of them (`--sizes` measures the rest)"
             if known else ", sizes not measured (`--sizes`)"))
    if prunable:
        print("`dev.py worktrees --prune` removes them; it asks first and never takes unmerged work.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:], repo=Path(__file__).resolve().parent.parent))
