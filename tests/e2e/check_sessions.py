#!/usr/bin/env python3
"""Concurrent ja2ctl sessions stay out of each other's way: several sessions in
one worktree, two worktree roots side by side, per-session logs and free ports.
Stale sessions of dead processes are reaped instead of looking alive.

    python tests/e2e/check_sessions.py
"""

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import uuid
from pathlib import Path

JA2CTL = Path(__file__).resolve().parent.parent.parent / "tools" / "ja2ctl.py"
# The shared log of the temp directory: a headless run must never write there.
SHARED_LOG = Path(tempfile.gettempdir()) / "ja2.log"


def ja2ctl(root: Path, session, *args: str) -> str:
    """Run ja2ctl with `root` as this worktree's session root, for `session`
    (None = the worktree's own default session)."""
    cmd = [sys.executable, str(JA2CTL)]
    if session:
        cmd += ["-s", session]
    cmd += ["--json", *args]
    result = subprocess.run(cmd, capture_output=True, text=True,
                            env=dict(os.environ, JA2CTL_SESSIONS=str(root)))
    if result.returncode != 0:
        raise SystemExit(f"ja2ctl {' '.join(args)} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def screen(root: Path, session) -> str:
    return json.loads(ja2ctl(root, session, "screen"))["result"]


def port(root: Path, session) -> int:
    return json.loads((root / session / "session.json").read_text(encoding="utf-8"))["port"]


def default_name(root: Path) -> str:
    """The name `ja2ctl start` (without -s) gave the session in `root`."""
    names = [d.name for d in root.iterdir() if d.is_dir() and (d / "session.json").exists()]
    assert len(names) == 1, f"one default session expected in {root}, got {names}"
    return names[0]


def log_file(root: Path, session) -> Path:
    return Path(ja2ctl(root, session, "log", "--path").strip())


def stale_session(root: Path, name: str):
    """A session registration whose game process is long gone: its pid was a
    throwaway process that has already exited."""
    proc = subprocess.Popen([sys.executable, "-c", "pass"])
    proc.wait()
    d = root / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "session.json").write_text(json.dumps({"port": 0, "pid": proc.pid}), encoding="utf-8")


def check_worktree_identity():
    """Session root and default session name derive from the worktree, and
    $JA2CTL_SESSIONS / $JA2CTL_SESSION override them."""
    spec = importlib.util.spec_from_file_location("ja2ctl_under_test", JA2CTL)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    saved = {k: os.environ.pop(k, None) for k in ("JA2CTL_SESSIONS", "JA2CTL_SESSION")}
    try:
        first = Path(tempfile.gettempdir()) / "worktree-a"
        mod.REPO = first
        root_a, name_a = mod.sessions_root(), mod.default_session_name()
        mod.REPO = Path(tempfile.gettempdir()) / "worktree-b"
        root_b, name_b = mod.sessions_root(), mod.default_session_name()
        mod.REPO = first
        assert (mod.sessions_root(), mod.default_session_name()) == (root_a, name_a), \
            "the same worktree always resolves to the same session root and name"
        assert root_a != root_b, "every worktree gets its own session root"
        assert name_a != name_b, "every worktree gets its own default session name"
        os.environ["JA2CTL_SESSIONS"] = str(Path(tempfile.gettempdir()) / "elsewhere")
        os.environ["JA2CTL_SESSION"] = "explicit"
        assert mod.sessions_root() == Path(tempfile.gettempdir()) / "elsewhere", "$JA2CTL_SESSIONS overrides the root"
        assert mod.default_session_name() == "explicit", "$JA2CTL_SESSION overrides the name"
    finally:
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    print("sessions: session root and name derive from the worktree")


def main() -> int:
    check_worktree_identity()
    marker_a = f"check-sessions-alpha-{uuid.uuid4()}"
    marker_run = f"check-sessions-run-{uuid.uuid4()}"
    with tempfile.TemporaryDirectory(prefix="ja2ctl-wt-a-", ignore_cleanup_errors=True) as a, \
         tempfile.TemporaryDirectory(prefix="ja2ctl-wt-b-", ignore_cleanup_errors=True) as b:
        root_a, root_b = Path(a), Path(b)  # two worktree roots, side by side
        try:
            # --- two sessions in one worktree root -------------------------
            ja2ctl(root_a, "alpha", "start")
            ja2ctl(root_a, "beta", "start")
            assert screen(root_a, "alpha") == screen(root_a, "beta") == "MAINMENU_SCREEN", "both boot to the main menu"
            assert port(root_a, "alpha") != port(root_a, "beta"), "each session gets a free port of its own"

            ja2ctl(root_a, "alpha", "click", "New Game")
            ja2ctl(root_a, "alpha", "wait-idle")
            assert screen(root_a, "alpha") == "GAME_INIT_OPTIONS_SCREEN", "alpha moved on"
            assert screen(root_a, "beta") == "MAINMENU_SCREEN", "beta is unaffected by alpha's input"

            ja2ctl(root_a, "beta", "click", "Preferences")
            ja2ctl(root_a, "beta", "wait-idle")
            assert screen(root_a, "beta") == "OPTIONS_SCREEN", "beta moved on"
            assert screen(root_a, "alpha") == "GAME_INIT_OPTIONS_SCREEN", "alpha is unaffected by beta's input"

            homes = [root_a / s / "home" for s in ("alpha", "beta")]
            assert all(h.is_dir() for h in homes), "each session has its own home"
            ja2ctl(root_a, "alpha", "eval", f'ja2.log("{marker_a}")')

            # --- a second worktree root, running at the same time -----------
            stale_session(root_b, "zombie")
            ja2ctl(root_b, None, "start")  # the worktree's own default session
            name_b = default_name(root_b)
            assert not (root_b / "zombie" / "session.json").exists(), "start reaps sessions of dead pids"
            assert screen(root_b, name_b) == "MAINMENU_SCREEN", "the other worktree root booted too"
            assert len({port(root_a, "alpha"), port(root_a, "beta"), port(root_b, name_b)}) == 3, \
                "no two sessions share a port"
            assert "alpha" not in ja2ctl(root_b, None, "list"), "one worktree root does not see the other's sessions"

            ja2ctl(root_a, "alpha", "key", "ESC")
            ja2ctl(root_a, "alpha", "wait-idle")
            assert screen(root_b, name_b) == "MAINMENU_SCREEN", "the other worktree is unaffected by our input"

            # --- stopping one worktree leaves the other alone ---------------
            stale_session(root_a, "ghost")
            ja2ctl(root_a, None, "stop", "--all")
            assert not (root_a / "ghost" / "session.json").exists(), "stop --all reaps sessions of dead pids"
            assert screen(root_b, name_b) == "MAINMENU_SCREEN", "stop --all stays inside its worktree root"

            stale_session(root_b, "zombie2")
            ja2ctl(root_b, None, "stop", "--all")
            assert not (root_b / "zombie2" / "session.json").exists(), "stop --all reaps sessions of dead pids"
        finally:
            for root in (root_a, root_b):
                subprocess.call([sys.executable, str(JA2CTL), "stop", "--all"],
                                env=dict(os.environ, JA2CTL_SESSIONS=str(root)))
        for root in (root_a, root_b):
            remaining = list(root.glob("*/session.json"))
            assert not remaining, f"sessions still registered after stop: {remaining}"

        # --- one log per session, and one per run --------------------------
        logs = {s: log_file(root_a, s) for s in ("alpha", "beta")}
        assert len(set(logs.values())) == 2, "each session logs to a file of its own"
        for name, log in logs.items():
            assert log == root_a / name / "ja2.log", f"{name} logs inside its own session directory"
            content = log.read_text(encoding="utf-8", errors="replace")
            assert content.strip(), f"{name} has a log"
            assert ja2ctl(root_a, name, "log").strip() == content.strip(), f"ja2ctl log prints {name}'s log"
        alpha_log = logs["alpha"].read_text(encoding="utf-8", errors="replace")
        beta_log = logs["beta"].read_text(encoding="utf-8", errors="replace")
        assert marker_a in alpha_log, "alpha's log holds alpha's messages"
        assert marker_a not in beta_log, "alpha's messages do not interleave into beta's log"

        script = root_b / "marker.lua"
        script.write_text(f'ja2.waitScreen("MAINMENU_SCREEN")\nja2.log("{marker_run}")\n', encoding="utf-8")
        ja2ctl(root_b, None, "run", str(script), "--keep")
        run_logs = sorted(root_b.glob("run-*.log"))
        assert len(run_logs) == 1, f"a run logs next to the sessions, not in a shared file: {run_logs}"
        content = run_logs[0].read_text(encoding="utf-8", errors="replace")
        assert marker_run in content, "the run wrote to its own log"
        assert not SHARED_LOG.exists() or marker_run not in SHARED_LOG.read_text(encoding="utf-8", errors="replace"), \
            "the run did not touch the shared ja2.log"
    print("sessions: concurrent worktrees kept their sessions, logs and ports apart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
