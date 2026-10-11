#!/usr/bin/env python3
"""shots.py - publish a PR's screenshots to the `pr-screenshots` branch.

PR proof used to be a five-step git-worktree dance typed by hand into
AGENTS.md, and every mistake it invites (an orphan branch that was never
actually orphaned, a force push over someone else's images, a half-finished
worktree left behind) is a PR that merges without proof. This is the dance,
once:

    python tools/dev.py shots <branch> <png...>

It fetches `pr-screenshots`, makes sure there is one worktree for it, copies
the images to `<branch>/` on it, commits, pushes, and prints the markdown
links to paste into the PR description.

`pr-screenshots` is an orphan branch: it holds images and nothing else, so
proof never enters `master`'s history and can be re-pointed without a
rebase. The worktree that publishes it is **detached** at the branch tip
rather than holding the branch itself, because the branch is usually checked
out somewhere else already - on this machine it was, in a stale `pr-shots`
worktree - and git refuses to check a branch out twice. Committing on a
detached HEAD and pushing `HEAD:pr-screenshots` sidesteps that, and a failed
push (someone published in between) rebases onto the new tip and retries
rather than clobbering it.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from urllib.parse import quote

DEFAULT_OWNER = "adhikasp"
DEFAULT_REPO = "ja2-pecel-bakwan"
BRANCH = "pr-screenshots"
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".webp", ".gif")
RAW = "https://raw.githubusercontent.com"

# Branch names become directories, so they get the same treatment as any other
# name a human might paste: `feature/123-ui` keeps its shape (that is how the
# links in README.md read), while anything a filesystem would choke on becomes
# a dash. Nothing here can escape the worktree.
UNSAFE = re.compile(r'[<>:"|?*\x00-\x1f]')


class ShotError(RuntimeError):
    """Something about the request or the repository makes publishing impossible."""


def git(*args: str, cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess:
    proc = subprocess.run(["git", *args], cwd=str(cwd) if cwd else None, capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise ShotError(f"git {' '.join(args)}: {(proc.stderr or proc.stdout).strip()}")
    return proc


def safe_branch_dir(branch: str) -> str:
    """A branch name as a relative directory under the screenshots branch."""
    cleaned = UNSAFE.sub("-", branch.strip()).strip(" .")
    parts = [p for p in cleaned.replace("\\", "/").split("/") if p not in ("", ".", "..")]
    if not parts:
        raise ShotError(f"{branch!r} is not usable as a directory name")
    return "/".join(parts)


def image_url(branch_dir: str, name: str, owner: str = DEFAULT_OWNER, repo: str = DEFAULT_REPO) -> str:
    return f"{RAW}/{owner}/{repo}/{BRANCH}/{quote(branch_dir)}/{quote(name)}"


def markdown_link(branch_dir: str, name: str, caption: str = "", owner: str = DEFAULT_OWNER,
                  repo: str = DEFAULT_REPO) -> str:
    alt = caption or Path(name).stem.replace("_", " ")
    return f"![{alt}]({image_url(branch_dir, name, owner, repo)})"


def slug_from_remote(url: str) -> tuple[str, str]:
    """A git remote URL -> (owner, repo), for the links the PR will carry.

    Handles the scp form git accepts (`git@github.com:owner/repo.git`) and the
    https form; anything else keeps its last two path segments, which is right
    for the local paths and file:// remotes a test uses.
    """
    text = url.strip().rstrip("/")
    if text.endswith(".git"):
        text = text[:-4]
    scp = re.match(r"^[^/\\]+@[^:/\\]+:(.*)$", text)   # user@host:owner/repo
    if scp:
        text = scp.group(1)
    parts = [p for p in text.replace("\\", "/").split("/") if p]
    if len(parts) < 2:
        return DEFAULT_OWNER, DEFAULT_REPO
    return parts[-2], parts[-1]


# --- the worktree ------------------------------------------------------------

def ref_exists(repo: Path, ref: str) -> bool:
    return git("show-ref", "--verify", "--quiet", ref, cwd=repo, check=False).returncode == 0


def parse_paths(porcelain: str) -> list[str]:
    return [line.split(" ", 1)[1] for line in porcelain.splitlines() if line.startswith("worktree ")]


def is_registered(repo: Path, path: Path) -> bool:
    """Whether `path` is a worktree of this repository, per git's own record."""
    listed = git("worktree", "list", "--porcelain", cwd=repo).stdout
    wanted = os.path.normcase(os.path.realpath(str(path)))
    return any(os.path.normcase(os.path.realpath(row)) == wanted for row in parse_paths(listed))


def prepare_worktree(repo: Path, work: Path, log=print) -> None:
    """Fetch the branch and make `work` a clean detached worktree at its tip.

    Creating it as an orphan the first time matters: a commit made on top of
    master would drag master's whole history into the screenshots branch, and
    that branch is supposed to hold images and nothing else.
    """
    git("fetch", "--quiet", "origin", BRANCH, cwd=repo, check=False)
    remote = f"refs/remotes/origin/{BRANCH}"
    if ref_exists(repo, remote):
        base = remote
    elif ref_exists(repo, f"refs/heads/{BRANCH}"):
        base = f"refs/heads/{BRANCH}"
    else:
        base = None

    if work.exists():
        if not is_registered(repo, work):
            raise ShotError(f"{work} exists but is not a worktree of this repository; move it aside")
        if base is None:
            # An orphan branch with no commits anywhere yet: start it empty.
            git("checkout", "--orphan", BRANCH, cwd=work)
            git("rm", "-rfq", "--ignore-unmatch", ".", cwd=work, check=False)
            git("clean", "-qfdx", cwd=work, check=False)
        else:
            # `--force` plus a clean: a previous run that stopped halfway (a dry
            # run, a rejected push) must not leave files behind in the branch.
            git("checkout", "--detach", "--force", base, cwd=work)
            git("clean", "-qfdx", cwd=work, check=False)
        return

    log(f"creating the {BRANCH} worktree at {work}")
    git("worktree", "add", "--detach", str(work), base or "HEAD", cwd=repo)
    if base is None:
        git("checkout", "--orphan", BRANCH, cwd=work)
        git("rm", "-rfq", "--ignore-unmatch", ".", cwd=work, check=False)
        git("clean", "-qfdx", cwd=work, check=False)
    git("reset", "--hard", cwd=work, check=False)


# --- publishing --------------------------------------------------------------

def collect_images(paths: list[str], cwd: Path) -> list[Path]:
    """Every image, resolved, checked and de-duplicated - before anything moves."""
    seen: dict[str, Path] = {}
    for raw in paths:
        candidate = Path(raw)
        if not candidate.is_absolute():
            candidate = cwd / candidate
        if not candidate.is_file():
            raise ShotError(f"no such file: {raw}")
        if candidate.suffix.lower() not in IMAGE_SUFFIXES:
            raise ShotError(f"{raw} is not an image this can publish"
                            f" (expected one of {', '.join(IMAGE_SUFFIXES)})")
        resolved = candidate.resolve()
        key = os.path.normcase(str(resolved))
        seen.setdefault(key, resolved)
    if not seen:
        raise ShotError("no images given")
    return list(seen.values())


def stage(work: Path, branch_dir: str, images: list[Path]) -> list[str]:
    """Copy the images in and add them; returns their names in the branch."""
    names = [image.name for image in images]
    # Two shots of different screens are often both called map.png; copying
    # them would leave one file and two identical links.
    if len({name.lower() for name in names}) != len(names):
        raise ShotError("two of these images have the same file name; rename one before publishing")
    target = work / branch_dir
    target.mkdir(parents=True, exist_ok=True)
    for image in images:
        shutil.copy2(image, target / image.name)
    git("add", "-A", "--", branch_dir, cwd=work)
    return names


def nothing_to_commit(work: Path, branch_dir: str) -> bool:
    return not git("status", "--porcelain", "--", branch_dir, cwd=work).stdout.strip()


def push(work: Path, branch_dir: str, images: list[Path], message: str,
         attempts: int = 3, log=print) -> str:
    """Commit and push, rebasing onto whatever landed meanwhile if we collide.

    Images are binaries, so a rebase can conflict when another agent published
    the same file; on a conflict the safe resolution is to take the remote as
    the base and re-apply our copy of the files, which is what the retry does.
    """
    last = ""
    for attempt in range(1, attempts + 1):
        git("commit", "-q", "-m", message, cwd=work)
        proc = git("push", "origin", f"HEAD:refs/heads/{BRANCH}", cwd=work, check=False)
        if proc.returncode == 0:
            return git("rev-parse", "--short", "HEAD", cwd=work).stdout.strip()
        last = (proc.stderr or proc.stdout).strip()
        if attempt == attempts:
            break
        log(f"push rejected, rebasing onto the new {BRANCH} tip and retrying "
            f"({attempt}/{attempts}): {last.splitlines()[-1] if last else 'git said no'}")
        git("fetch", "--quiet", "origin", BRANCH, cwd=work, check=False)
        if not ref_exists(work, f"refs/remotes/origin/{BRANCH}"):
            break
        git("reset", "--hard", f"refs/remotes/origin/{BRANCH}", cwd=work, check=False)
        stage(work, branch_dir, images)
    raise ShotError(f"could not push to {BRANCH}: {last}")


def publish(repo: Path, work: Path, branch: str, images: list[Path], *, message: str = "",
            owner: str = DEFAULT_OWNER, name: str = DEFAULT_REPO, log=print,
            dry_run: bool = False) -> list[str]:
    """Put `images` on the screenshots branch under `branch`; return markdown."""
    branch_dir = safe_branch_dir(branch)
    message = message or f"Screenshots for {branch}"
    prepare_worktree(repo, work, log)
    names = stage(work, branch_dir, images)
    if nothing_to_commit(work, branch_dir):
        log(f"{branch_dir} already holds these images on {BRANCH}; nothing to publish")
    elif dry_run:
        log(f"dry run: {len(names)} image(s) ready for {BRANCH}/{branch_dir}, nothing pushed")
    else:
        sha = push(work, branch_dir, images, message, log=log)
        log(f"pushed {len(names)} image(s) to {BRANCH}/{branch_dir} as {sha}")
    return [markdown_link(branch_dir, n, owner=owner, repo=name) for n in names]


# --- the command -------------------------------------------------------------

def default_work_dir() -> Path:
    """Where the screenshots worktree lives: machine-wide, never per checkout.

    One shared location is the point - the old dance created a `pr-shots`
    worktree next to every checkout that ran it, which is where the pile of
    stale ones came from. dev.py passes its own `state_dir()` here; this
    mirrors it for the standalone `python tools/shots.py` path.
    """
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or (Path.home() / "AppData" / "Local")
        return Path(base) / "ja2-dev" / BRANCH
    return Path.home() / ".ja2-dev" / BRANCH


def main(argv: list[str] | None = None, *, repo: Path, work: Path | None = None, log=print) -> int:
    parser = argparse.ArgumentParser(
        prog="dev.py shots",
        description="publish PR screenshots to the orphan pr-screenshots branch")
    parser.add_argument("branch", help="the branch the PR is from; images land in <branch>/")
    parser.add_argument("images", nargs="+", help="PNG files to publish")
    parser.add_argument("-m", "--message", default="", help="commit message (default: Screenshots for <branch>)")
    parser.add_argument("--owner", default=None, help="repository owner for the links (default: the origin remote)")
    parser.add_argument("--repo-name", default=None, help="repository name for the links")
    parser.add_argument("--dry-run", action="store_true", help="copy and stage, but do not commit or push")
    args = parser.parse_args(argv)

    if not shutil.which("git"):
        log("error: git not found on PATH")
        return 1
    work = Path(work) if work else default_work_dir()
    try:
        remote = git("remote", "get-url", "origin", cwd=repo).stdout.strip()
    except ShotError:
        log(f"error: no 'origin' remote in {repo}")
        return 1
    slug_owner, slug_repo = slug_from_remote(remote) if remote else ("", "")
    if not args.owner:
        args.owner = slug_owner or DEFAULT_OWNER
    if not args.repo_name:
        args.repo_name = slug_repo or DEFAULT_REPO
    if args.owner != DEFAULT_OWNER or args.repo_name != DEFAULT_REPO:
        log(f"warning: origin is {args.owner}/{args.repo_name}, not {DEFAULT_OWNER}/{DEFAULT_REPO}"
            " - are the screenshots meant for another fork?")

    try:
        images = collect_images(args.images, Path.cwd())
        links = publish(repo, work, args.branch, images, message=args.message,
                        owner=args.owner, name=args.repo_name, log=log, dry_run=args.dry_run)
    except ShotError as exc:
        log(f"error: {exc}")
        return 1

    print("\nPaste into the PR description:\n")
    for link in links:
        print(link)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:], repo=Path(__file__).resolve().parent.parent))
