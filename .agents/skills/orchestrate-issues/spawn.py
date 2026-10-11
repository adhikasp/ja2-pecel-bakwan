#!/usr/bin/env python3
"""Spawn one OpenCode agent session for the orchestrator skill.

Runs `opencode run` in a worktree with a prompt read from a file, streams the
session to a log, and prints the exit code. Works on macOS and Windows without
shell quoting hazards -- the prompt is never passed through a shell.

    python spawn.py --worktree <dir> --prompt <file> [--agent researcher]
                    [--title T] [--wait] [--timeout-minutes N]

Without --wait it returns as soon as the process is launched (a background
session); with --wait it blocks until the agent finishes and then prints the
log's tail plus the agent's final message.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_MODEL = "opencode/space-bunny-free"

# Where the OpenCode CLI lives on each platform. `opencode` on PATH wins; the
# desktop app ships its own copy that is not on PATH.
WINDOWS_FALLBACK = (
    Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "@opencodedesktop"
    / "resources" / "opencode-cli.exe"
)
DARWIN_FALLBACK = Path("/Applications/OpenCode.app/Contents/Resources/opencode-cli")


def find_cli() -> str:
    for name in ("opencode", "opencode-cli", "opencode-cli.exe"):
        found = shutil.which(name)
        if found:
            return found
    for candidate in (WINDOWS_FALLBACK, DARWIN_FALLBACK):
        if candidate.is_file():
            return str(candidate)
    sys.exit(
        "opencode CLI not found. Install it, or add it to PATH, or set "
        "OPENCODE_CLI to its full path."
    )


def git(worktree: str, *args: str) -> str:
    out = subprocess.run(
        ["git", *args], cwd=worktree, capture_output=True, text=True, check=True
    )
    return out.stdout.strip()


def session_id_from_log(log: Path) -> str | None:
    """Best-effort scrape of the session id OpenCode prints in its log."""
    try:
        text = log.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    import re

    match = re.search(r"\bses_[A-Za-z0-9]{16,}\b", text)
    return match.group(0) if match else None


def main() -> int:
    # The log tail is agent output and may contain any character; a legacy
    # Windows console (cp1252) must not turn a finished run into a crash.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worktree", required=True, help="directory to run the agent in")
    parser.add_argument("--prompt", required=True, help="file holding the agent prompt")
    parser.add_argument("--agent", default=None, help="agent name (omit for the default build agent)")
    parser.add_argument("--model", default=os.environ.get("ORCHESTRATOR_MODEL", DEFAULT_MODEL))
    parser.add_argument("--title", default=None, help="session title")
    parser.add_argument("--wait", action="store_true", help="block until the agent finishes")
    parser.add_argument("--timeout-minutes", type=int, default=0, help="0 = no limit")
    args = parser.parse_args()

    worktree = Path(args.worktree).resolve()
    prompt_file = Path(args.prompt).resolve()
    if not worktree.is_dir():
        sys.exit(f"worktree does not exist: {worktree}")
    if not prompt_file.is_file():
        sys.exit(f"prompt file does not exist: {prompt_file}")

    # One agent per worktree: refuse to double up.
    branch = git(str(worktree), "rev-parse", "--abbrev-ref", "HEAD")
    slug = f"{branch.replace('/', '-')}-{int(time.time())}"
    log_dir = Path.home() / ".ja2-orch" / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log = log_dir / f"{slug}.log"

    cli = os.environ.get("OPENCODE_CLI") or find_cli()
    title = args.title or f"orchestrate {branch}"

    # The prompt goes in a file the agent reads; only this short line is passed
    # on the command line, so no quoting or newline can corrupt it.
    message = f"Read {prompt_file} and carry out exactly what it says."

    argv = [cli, "run", "--model", args.model, "--auto", "--title", title]
    if args.agent:
        argv += ["--agent", args.agent]
    argv.append(message)

    with log.open("w", encoding="utf-8", buffering=1) as handle:
        handle.write(f"# {title}\n# worktree: {worktree}\n# argv: {argv}\n\n")
        handle.flush()
        proc = subprocess.Popen(
            argv,
            cwd=str(worktree),
            stdout=handle,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            text=True,
        )

    print(f"worktree: {worktree}")
    print(f"log:      {log}")
    print(f"branch:   {branch}")
    print(f"pid:      {proc.pid}")

    if not args.wait:
        print("state:    RUNNING (launched; poll the log or the issue comments)")
        return 0

    deadline = time.time() + args.timeout_minutes * 60 if args.timeout_minutes else None
    while True:
        try:
            proc.wait(timeout=30)
            break
        except subprocess.TimeoutExpired:
            if deadline and time.time() > deadline:
                proc.kill()
                print(f"state:    TIMEOUT after {args.timeout_minutes} min", flush=True)
                return 124
            print(f"... still running ({int(time.time())}s elapsed)", flush=True)

    sid = session_id_from_log(log)
    if sid:
        print(f"session:  {sid}")
    print(f"exit:     {proc.returncode}")
    print(f"state:    {'FINISHED' if proc.returncode == 0 else 'FAILED'}")
    print("--- log tail ---")
    try:
        tail = log.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]
        print("\n".join(tail))
    except OSError as exc:  # pragma: no cover
        print(f"(could not read log: {exc})")
    return proc.returncode or 0


if __name__ == "__main__":
    sys.exit(main())