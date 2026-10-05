#!/usr/bin/env python3
"""ja2ctl - drive JA2 Pecel Bakwan sessions from the command line.

A session is a headless game process (ja2 -serve) with its own home directory,
log and saves. The game only advances while a command steps it, so you can take
as long as you like between commands.

    ja2ctl start                      # boot this worktree's session
    ja2ctl text                       # what text is on screen?
    ja2ctl ui                         # what can be clicked?
    ja2ctl click "New Game"           # click by label (or: ja2ctl click 320 240)
    ja2ctl shot                       # save a screenshot, print its path
    ja2ctl log                        # the session's game log
    ja2ctl stop

    ja2ctl -s alpha start --load SaveGame01   # several sessions side by side
    ja2ctl -s alpha state

    ja2ctl run tests/e2e/smoke.lua    # one-shot script run, no session

Run `ja2ctl help` for all commands. Only the Python standard library is used.
"""

import argparse
import hashlib
import json
import re
import os
import shutil
import socket
import subprocess
import tempfile
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# Console log lines below warning level: "<timestamp> [INFO] ..." (possibly with colour codes).
LOG_NOISE = re.compile(r"^\d{4}-\d\d-\d\dT\S+\s+(\x1b\[[0-9;]*m)*\[(INFO|DEBUG|TRACE)\]")
IS_WINDOWS = os.name == "nt"


# --- locating things ------------------------------------------------------

def windows_folder(guid: str):
    """A Windows known folder, for shells (e.g. an MSYS2 login shell) that do
    not pass APPDATA/USERPROFILE on."""
    import ctypes
    import uuid
    from ctypes import wintypes

    class GUID(ctypes.Structure):
        _fields_ = [("data", ctypes.c_byte * 16)]

    folder = GUID()
    ctypes.memmove(folder.data, uuid.UUID(guid).bytes_le, 16)
    path = ctypes.c_wchar_p()
    get = ctypes.windll.shell32.SHGetKnownFolderPath
    get.argtypes = [ctypes.POINTER(GUID), wintypes.DWORD, wintypes.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
    if get(ctypes.byref(folder), 0, None, ctypes.byref(path)) != 0:
        return None
    try:
        return Path(path.value)
    finally:
        ctypes.windll.ole32.CoTaskMemFree(path)


def user_home() -> Path:
    try:
        return Path.home()
    except RuntimeError:
        if IS_WINDOWS:
            profile = windows_folder("5E6C858F-0E22-4760-9AFE-EA3317B67173")  # FOLDERID_Profile
            if profile:
                return profile
        raise


def worktree_id() -> str:
    """Id of the checkout this ja2ctl belongs to: the repo folder name plus a
    short hash of its path. Two worktrees (or two clones) never collide."""
    digest = hashlib.sha1(os.path.normcase(str(REPO)).encode("utf-8", "surrogateescape")).hexdigest()[:8]
    name = re.sub(r"[^A-Za-z0-9._-]", "_", REPO.name) or "worktree"
    return f"{name}-{digest}"


def sessions_root() -> Path:
    """Where this worktree's sessions live. One root per worktree, so agents in
    other worktrees can neither see, stop nor reuse our sessions. Override the
    whole root with $JA2CTL_SESSIONS."""
    root = os.environ.get("JA2CTL_SESSIONS")
    return Path(root) if root else user_home() / ".ja2ctl" / "sessions" / worktree_id()


def default_session_name() -> str:
    """Also per worktree, so even a shared $JA2CTL_SESSIONS keeps worktrees apart.
    Override with -s or $JA2CTL_SESSION."""
    return os.environ.get("JA2CTL_SESSION") or worktree_id()


def session_dir(name: str) -> Path:
    return sessions_root() / name


def session_names() -> list:
    """Session directories in this worktree's root (run logs are plain files)."""
    root = sessions_root()
    if not root.is_dir():
        return []
    return sorted(d.name for d in root.iterdir() if d.is_dir())


def find_binary() -> Path:
    env = os.environ.get("JA2_BIN")
    if env:
        return Path(env)
    exe = "ja2.exe" if IS_WINDOWS else "ja2"
    for build in ("_bin", "build", "cmake-build-debug", "cmake-build-release"):
        candidate = REPO / build / exe
        if candidate.exists():
            return candidate
    found = shutil.which("ja2")
    if found:
        return Path(found)
    sys.exit("ja2ctl: cannot find the ja2 binary; set JA2_BIN")


def default_ja2_home() -> Path:
    if IS_WINDOWS:
        appdata = os.environ.get("APPDATA")
        roaming = Path(appdata) if appdata else windows_folder("3EB685DB-65F9-4CF6-A03A-E3EF65729F3D")  # FOLDERID_RoamingAppData
        return (roaming or user_home() / "AppData" / "Roaming") / "JA2"
    return user_home() / ".ja2"


def default_game_dir():
    config = default_ja2_home() / "ja2.json"
    try:
        return json.loads(config.read_text(encoding="utf-8")).get("game_dir")
    except (OSError, ValueError):
        return None


def child_env() -> dict:
    env = dict(os.environ)
    if IS_WINDOWS:
        # MSYS2 builds need the MinGW runtime DLLs (SDL3, libstdc++, ...).
        mingw = Path(os.environ.get("MSYS2_ROOT", r"C:\msys64")) / "mingw64" / "bin"
        if mingw.exists():
            env["PATH"] = str(mingw) + os.pathsep + env.get("PATH", "")
    return env


def prepare_home(home: Path, game_dir, saves):
    """Give the session its own ja2.json so it never touches the user's."""
    home.mkdir(parents=True, exist_ok=True)
    config = {}
    game_dir = game_dir or default_game_dir()
    if game_dir:
        config["game_dir"] = game_dir
    if saves:
        config["save_game_dir"] = str(Path(saves).resolve())
    (home / "ja2.json").write_text(json.dumps(config, indent=2), encoding="utf-8")


def stage_save(home: Path, saves, load):
    """--load may name a save file anywhere; copy it where the session looks."""
    if not load or not (load.endswith(".sav") or os.sep in load or "/" in load):
        return load
    src = Path(load)
    if not src.exists():
        sys.exit(f"ja2ctl: no such save file: {load}")
    target_dir = Path(saves) if saves else home / "SavedGames"
    target_dir.mkdir(parents=True, exist_ok=True)
    if src.resolve().parent != target_dir.resolve():
        shutil.copy2(src, target_dir / src.name)
    return src.stem


def game_args(opts) -> list:
    """Options shared by `start` and `run`."""
    args = []
    if opts.seed is not None:
        args += ["-seed", str(opts.seed)]
    if opts.show:
        args.append("-show")
    if not opts.intro:
        args.append("-no-intro")
    if opts.res:
        args += ["-res", opts.res]
    if getattr(opts, "uiscale", None):
        args += ["-uiscale", opts.uiscale]
    if getattr(opts, "worldzoom", None):
        args += ["-worldzoom", opts.worldzoom]
    if getattr(opts, "freeze_wall_clock", None):
        args += ["-freeze-wall-clock", opts.freeze_wall_clock]
    if getattr(opts, "version_label", None):
        args += ["-version-label", opts.version_label]
    if opts.timeout:
        args += ["-timeout", str(opts.timeout)]
    return args


# --- talking to a session ---------------------------------------------------

class SessionError(Exception):
    pass


def read_session(name: str) -> dict:
    info = session_dir(name) / "session.json"
    try:
        return json.loads(info.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        raise SessionError(f'no running session "{name}" (start one with: ja2ctl -s {name} start)')


def read_session_or_none(name: str):
    try:
        return read_session(name)
    except SessionError:
        return None


def request(name: str, payload: dict, timeout: float = 600.0) -> dict:
    info = read_session(name)
    payload = dict(payload, id=1)
    try:
        with socket.create_connection(("127.0.0.1", info["port"]), timeout=timeout) as sock:
            sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
            data = b""
            while not data.endswith(b"\n"):
                chunk = sock.recv(65536)
                if not chunk:
                    break
                data += chunk
    except OSError as e:
        raise SessionError(f'session "{name}" is not answering ({e}); is it still running? see {session_dir(name)}')
    if not data:
        raise SessionError(f'session "{name}" closed the connection')
    return json.loads(data.decode("utf-8"))


def call(name: str, fn: str, *args):
    return request(name, {"call": fn, "args": list(args)})


def lua_quote(s: str) -> str:
    return json.dumps(s)  # a JSON string literal is a valid Lua string literal


# --- output -----------------------------------------------------------------

def status_line(reply: dict) -> str:
    idle = "idle" if reply.get("idle") else "busy"
    return f'[{reply.get("screen")} frame {reply.get("frame")} t={reply.get("ms")}ms {idle}]'


def print_result(reply: dict, raw: bool, formatter=None):
    if raw:
        print(json.dumps(reply, indent=2))
        return 0 if reply.get("ok") else 1
    if not reply.get("ok"):
        print(f'error ({reply.get("kind")}): {reply.get("error")}', file=sys.stderr)
        print(status_line(reply), file=sys.stderr)
        return {"timeout": 4, "crash": 3, "expectation": 1}.get(reply.get("kind"), 2)
    result = reply.get("result")
    if formatter:
        formatter(result)
    elif result is None:
        pass
    elif isinstance(result, (dict, list)):
        print(json.dumps(result, indent=2, ensure_ascii=False))
    else:
        print(result)
    print(status_line(reply), file=sys.stderr)
    return 0


def format_ui(items):
    for e in items or []:
        flags = "" if e.get("enabled") else " (disabled)"
        extra = f' help="{e["help"]}"' if e.get("help") and e.get("help") != e.get("label") else ""
        nid = f' #{e["id"]}' if e.get("kind") == "native" else ""
        focus = " (focused)" if e.get("focused") else ""
        print(f'{e["kind"]:6} {e["x"]:4},{e["y"]:<4} {e["w"]:3}x{e["h"]:<3}{nid} "{e.get("label", "")}"{extra}{flags}{focus}')


def format_text(items):
    for t in items or []:
        print(f'{t["x"]:4},{t["y"]:<4} {t["text"]}')


def format_state(s):
    t = s.get("time", {})
    print(f'screen   {s.get("screen")}  (idle: {s.get("idle")})')
    if s.get("messageBox"):
        print(f'message  "{s.get("messageBoxText", "")}"')
    print(f'time     day {t.get("day")} {t.get("hour", 0):02}:{t.get("minute", 0):02}' + (" (paused)" if t.get("paused") else ""))
    print(f'money    ${s.get("money")}')
    print(f'sector   {s.get("sector")}')
    tac = s.get("tactical", {})
    if tac.get("inCombat"):
        print(f'combat   yes, team {tac.get("currentTeam")} to move')
    mercs = s.get("mercs") or []
    print(f'mercs    {len(mercs)}')
    for m in mercs:
        print(f'  {m["name"]:12} {m["sector"]:5} {m.get("assignmentName", m["assignment"])!s:14} HP {m["life"]}/{m["lifeMax"]}')


# --- commands -----------------------------------------------------------------

def cmd_start(opts):
    name = opts.session
    d = session_dir(name)
    for stale in reap_stale():
        print(f'reaped stale session "{stale}"')
    try:
        read_session(name)
        request(name, {"lua": "return true"}, timeout=5)
        sys.exit(f'ja2ctl: session "{name}" is already running (ja2ctl -s {name} stop)')
    except SessionError:
        pass
    if d.exists():
        shutil.rmtree(d, ignore_errors=True)
    home = d / "home"
    prepare_home(home, opts.game_dir, opts.saves)
    load = stage_save(home, opts.saves, opts.load)

    args = [str(find_binary()), "-serve", "0",   # 0 = the OS hands out a free port, never a fixed one
            "-session-file", str(d / "session.json"),
            "-home", str(home), "-log", str(d / "ja2.log"), "-out", str(d),
            # so that require("lib.campaign") works in `ja2ctl eval`
            "-lua-path", str(REPO / "tests" / "e2e")]
    args += game_args(opts)
    if load:
        args += ["-load", load]
    args += opts.extra

    out = open(d / "stdout.log", "wb")
    kwargs = {}
    if IS_WINDOWS:
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
    else:
        kwargs["start_new_session"] = True
    proc = subprocess.Popen(args, cwd=find_binary().parent, stdout=out, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, env=child_env(), **kwargs)

    deadline = time.time() + opts.wait
    while time.time() < deadline:
        if (d / "session.json").exists():
            info = read_session(name)
            print(f'session "{name}" ready on port {info["port"]} (pid {info["pid"]}), files in {d}')
            reply = request(name, {"lua": "return ja2.screen()"})
            print(status_line(reply))
            return 0
        if proc.poll() is not None:
            tail = (d / "stdout.log").read_text(encoding="utf-8", errors="replace").splitlines()[-25:]
            print("\n".join(tail), file=sys.stderr)
            sys.exit(f'ja2ctl: the game exited with code {proc.returncode} before it was ready; log: {d / "ja2.log"}')
        time.sleep(0.2)
    sys.exit(f"ja2ctl: session did not come up within {opts.wait}s; see {d}")


def wait_for_exit(pid: int, timeout: float) -> bool:
    """True once process `pid` has exited (it may keep files open until then)."""
    if IS_WINDOWS:
        import ctypes
        SYNCHRONIZE = 0x00100000
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(SYNCHRONIZE, False, pid)
        if not handle:
            return True  # already gone
        try:
            return kernel32.WaitForSingleObject(handle, int(timeout * 1000)) == 0
        finally:
            kernel32.CloseHandle(handle)
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        except PermissionError:
            pass
        time.sleep(0.05)
    return False


def pid_alive(pid) -> bool:
    """Is process `pid` running? (A dead session's pid may be reused by an
    unrelated process later; never kill a pid that is not answering for us.)"""
    if not pid:
        return False
    if IS_WINDOWS:
        import ctypes
        from ctypes import wintypes
        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        ERROR_ACCESS_DENIED = 5
        STILL_ACTIVE = 259
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, int(pid))
        if not handle:
            return kernel32.GetLastError() == ERROR_ACCESS_DENIED
        try:
            code = wintypes.DWORD()
            return bool(kernel32.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(int(pid), 0)
    except OSError:
        return False
    return True


def port_open(port) -> bool:
    try:
        with socket.create_connection(("127.0.0.1", int(port)), timeout=2):
            return True
    except OSError:
        return False


def reap_stale() -> list:
    """Drop the registration of every session whose game process is gone, so
    stale sessions never look running or block their name. Returns their names
    (their directories stay behind: the log is worth keeping)."""
    gone = []
    for name in session_names():
        info = read_session_or_none(name)
        if info is None or pid_alive(info.get("pid")):
            continue
        (session_dir(name) / "session.json").unlink(missing_ok=True)
        gone.append(name)
    return gone


def cmd_stop(opts):
    for stale in reap_stale():
        print(f'reaped stale session "{stale}"')
    names = session_names() if opts.all else [opts.session]
    for name in names:
        info = read_session_or_none(name)
        if info is None:
            if not opts.all:
                print(f'"{name}" is not running')
            continue
        if not pid_alive(info.get("pid")):
            (session_dir(name) / "session.json").unlink(missing_ok=True)
            print(f'reaped stale session "{name}"')
            continue
        # Only force-kill a process that answers on its own session port; a pid
        # of a long-dead session may have been reused by something unrelated.
        ours = port_open(info["port"])
        try:
            request(name, {"call": "shutdown"}, timeout=10)
        except SessionError:
            pass
        if not wait_for_exit(info["pid"], 15):
            if not ours:
                print(f'"{name}" (pid {info["pid"]}) is alive but not serving port {info["port"]}; '
                      f'leaving it alone', file=sys.stderr)
                continue
            try:
                os.kill(info["pid"], 9)
            except OSError:
                pass
            wait_for_exit(info["pid"], 5)
        (session_dir(name) / "session.json").unlink(missing_ok=True)
        print(f'stopped "{name}"')
    return 0


def cmd_list(opts):
    root = sessions_root()
    names = session_names()
    if not names:
        print(f"no sessions in {root}")
        return 0
    for name in names:
        info = read_session_or_none(name)
        if info is None:
            print(f"{name:24} (not running)")
            continue
        try:
            reply = request(name, {"lua": "return true"}, timeout=5)
            print(f'{name:24} port {info["port"]:5}  {status_line(reply)}')
        except SessionError:
            print(f"{name:24} (not running)")
    return 0


def cmd_run(opts):
    args = [str(find_binary()), "-run", str(Path(opts.script).resolve())]
    home = opts.home
    scratch = None
    if home is None and opts.isolated:
        # A fresh home per run: no saves or settings leak between runs.
        scratch = tempfile.mkdtemp(prefix="ja2ctl-run-")
        home = scratch
    if home:
        home = Path(home).resolve()
        prepare_home(home, opts.game_dir, opts.saves)
        log = home / "ja2.log"
        args += ["-home", str(home), "-log", str(log)]
    else:
        # Never the shared ja2.log of the temp directory: every run gets a log of
        # its own, next to this worktree's sessions.
        sessions_root().mkdir(parents=True, exist_ok=True)
        log = sessions_root() / f"run-{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}.log"
        args += ["-log", str(log)]
    load = stage_save(home if home else default_ja2_home(), opts.saves, opts.load)
    if load:
        args += ["-load", load]
    if opts.out:
        args += ["-out", str(Path(opts.out).resolve())]
    for a in opts.arg or []:
        args += ["-arg", a]
    args += game_args(opts)
    args += opts.extra
    if opts.verbose:
        code = subprocess.call(args, cwd=find_binary().parent, env=child_env())
    else:
        # The game echoes its whole log to the console; keep warnings, errors
        # and the script's own output (the full log is in the log file).
        proc = subprocess.Popen(args, cwd=find_binary().parent, env=child_env(),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for raw in proc.stdout:
            line = raw.decode("utf-8", errors="replace")
            if LOG_NOISE.match(line):
                continue
            sys.stdout.write(line)
            sys.stdout.flush()
        code = proc.wait()
    if scratch:
        if code == 0 and not opts.keep:
            shutil.rmtree(scratch, ignore_errors=True)
        else:
            print(f"ja2ctl: home directory kept for inspection: {scratch}", file=sys.stderr)
    elif home is None:
        if code == 0 and not opts.keep:
            Path(log).unlink(missing_ok=True)  # a passing run leaves no junk behind
        else:
            print(f"ja2ctl: log kept: {log}", file=sys.stderr)
    elif code != 0:
        print(f"ja2ctl: log: {log}", file=sys.stderr)
    return code


def cmd_log(opts):
    log = session_dir(opts.session) / "ja2.log"
    if opts.path:
        print(log)
        return 0
    if not log.exists():
        raise SessionError(f'no log for "{opts.session}" yet ({log})')
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    if opts.lines:
        lines = lines[-opts.lines:]
    print("\n".join(lines))
    return 0


def locator_args(opts):
    if len(opts.target) == 2 and all(t.lstrip("-").isdigit() for t in opts.target):
        return [int(opts.target[0]), int(opts.target[1])]
    # "#credits.back" (or --id credits.back): a native element by id
    if getattr(opts, "id", False) or (len(opts.target) == 1 and opts.target[0].startswith("#")):
        return [{"id": opts.target[0].lstrip("#")}]
    loc = {"text": " ".join(opts.target)}
    if opts.exact:
        loc["exact"] = True
    if opts.index != 1:
        loc["index"] = opts.index
    return [loc]


def build_parser():
    p = argparse.ArgumentParser(prog="ja2ctl", description=__doc__.split("\n\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-s", "--session", default=os.environ.get("JA2CTL_SESSION") or default_session_name(),
                   help="session name (default: $JA2CTL_SESSION, or derived from this worktree)")
    p.add_argument("--json", action="store_true", help="print the raw JSON reply")
    sub = p.add_subparsers(dest="cmd", metavar="COMMAND")

    def game_opts(sp):
        sp.add_argument("--load", help="save name, or path to a .sav file")
        sp.add_argument("--seed", type=int)
        sp.add_argument("--show", action="store_true", help="show a window instead of headless")
        sp.add_argument("--intro", action="store_true", help="play the splash/intro videos")
        sp.add_argument("--res", help="resolution, e.g. 1280x720")
        sp.add_argument("--uiscale", help="UI scale 1-4 (headless: only together with --worldzoom)")
        sp.add_argument("--worldzoom", help="world zoom 1-4 or match_ui: runs the world as a layer of its own (headless too)")
        sp.add_argument("--freeze-wall-clock", dest="freeze_wall_clock",
                        metavar="INSTANT",
                        help="pin the wall clock so the run does not depend on when it ran: seconds "
                             "since the epoch, or an ISO-8601 UTC instant (2001-02-03T04:05:06Z). "
                             "Times shown to the player are then rendered in UTC.")
        sp.add_argument("--version-label", metavar="TEXT",
                        help="version string the UI shows (default: this build's, commit sha included)")
        sp.add_argument("--saves", help="save game directory to use (shared with other sessions)")
        sp.add_argument("--game-dir", help="JA2 data directory (default: from your ja2.json)")
        sp.add_argument("--timeout", type=float, help="kill the game after this many wall-clock seconds")
        sp.add_argument("extra", nargs="*", help="extra ja2 arguments (after --)")

    sp = sub.add_parser("start", help="start a session")
    game_opts(sp)
    sp.add_argument("--wait", type=float, default=180, help="seconds to wait for the game to boot")
    sp = sub.add_parser("stop", help="stop a session")
    sp.add_argument("--all", action="store_true")
    sub.add_parser("list", help="list sessions")
    sp = sub.add_parser("log", help="the session's game log")
    sp.add_argument("-n", "--lines", type=int, help="only the last N lines")
    sp.add_argument("--path", action="store_true", help="print the log file's path instead of its content")

    sp = sub.add_parser("run", help="run a Lua script in a fresh one-shot game and exit with its result")
    sp.add_argument("script")
    game_opts(sp)
    sp.add_argument("--arg", action="append", help="value for ja2.args (repeatable)")
    sp.add_argument("--out", help="directory for screenshots")
    sp.add_argument("-v", "--verbose", action="store_true", help="show the game's full console log")
    sp.add_argument("--home", help="config/save directory (default: your normal JA2 home)")
    sp.add_argument("--isolated", action="store_true", help="use a fresh throwaway home directory")
    sp.add_argument("--keep", action="store_true", help="keep what a passing run throws away: the --isolated home, the run's log")

    sub.add_parser("screen", help="current screen")
    sub.add_parser("state", help="game state summary")
    sp = sub.add_parser("ui", help="clickable elements")
    sp.add_argument("--all", action="store_true", help="include unlabelled and covered regions")
    sub.add_parser("text", help="text visible on screen")
    sub.add_parser("audit", help="layout problems (legacy regions off screen, native layout audit)")
    sub.add_parser("native", help="native UI state: renderer, size, scale, screen, ui_mode per screen")
    sp = sub.add_parser("uimode", help="set a screen's ui_mode for this session: uimode credits legacy|native|default")
    sp.add_argument("key")
    sp.add_argument("mode")
    sp = sub.add_parser("uiscale", help="native UI scale for this session, e.g. 1.5")
    sp.add_argument("scale", type=float)
    sp = sub.add_parser("vm", help="a view model's fields, e.g. vm status / vm credits")
    sp.add_argument("name")

    for verb, fn in (("click", "click"), ("rclick", "rclick"), ("dblclick", "dblclick"), ("hover", "hover"),
                     ("wait-for", "waitFor"), ("find", "find")):
        sp = sub.add_parser(verb, help=f"{fn} by label/text, or at X Y")
        sp.add_argument("target", nargs="+")
        sp.add_argument("--exact", action="store_true", help="whole-label match")
        sp.add_argument("--index", type=int, default=1, help="n-th match in reading order")
        sp.add_argument("--id", action="store_true", help="TARGET is a native element id (same as #ID)")
        sp.set_defaults(fn=fn)
    sp = sub.add_parser("wait-gone", help="wait until a label/text disappears")
    sp.add_argument("target", nargs="+")
    sp.add_argument("--id", action="store_true")
    sp.add_argument("--exact", action="store_true")
    sp.add_argument("--index", type=int, default=1)

    sp = sub.add_parser("key", help="press a key or combo, e.g. ESC, enter, alt+c")
    sp.add_argument("combo")
    sp.add_argument("times", nargs="?", type=int, default=1)
    sp = sub.add_parser("type", help="type text")
    sp.add_argument("text")
    sp = sub.add_parser("move", help="move the mouse")
    sp.add_argument("x", type=int)
    sp.add_argument("y", type=int)
    sp = sub.add_parser("drag", help="drag with the left button")
    for a in ("x0", "y0", "x1", "y1"):
        sp.add_argument(a, type=int)
    sp = sub.add_parser("wheel", help="scroll the mouse wheel (+up / -down)")
    sp.add_argument("dy", type=int)

    sp = sub.add_parser("step", help="advance N frames")
    sp.add_argument("frames", nargs="?", type=int, default=1)
    sp = sub.add_parser("wait", help="advance N milliseconds of game time")
    sp.add_argument("ms", type=int)
    sp = sub.add_parser("wait-idle", help="advance until nothing is animating/loading")
    sp.add_argument("ms", nargs="?", type=int)
    sp = sub.add_parser("wait-screen", help="advance until a screen is showing and idle")
    sp.add_argument("name")
    sp.add_argument("ms", nargs="?", type=int)

    sp = sub.add_parser("shot", help="save a screenshot (PNG) and print its path")
    sp.add_argument("path", nargs="?")
    sp = sub.add_parser("pixel", help="colour of a pixel")
    sp.add_argument("x", type=int)
    sp.add_argument("y", type=int)

    sub.add_parser("saves", help="list loadable saves")
    sp = sub.add_parser("load", help="load a save")
    sp.add_argument("name")
    sp = sub.add_parser("save", help="save the game")
    sp.add_argument("name")
    sp.add_argument("description", nargs="?")

    sp = sub.add_parser("eval", help="run Lua in the session (use `return` to get a value)")
    sp.add_argument("code")
    sub.add_parser("help", help="show this help")
    return p


def main(argv=None):
    parser = build_parser()
    if argv is None:
        argv = sys.argv[1:]
    # Everything after "--" goes to the game untouched.
    extra = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    opts = parser.parse_args(argv)
    if hasattr(opts, "extra"):
        opts.extra = (opts.extra or []) + extra
    name = opts.session
    raw = opts.json

    try:
        cmd = opts.cmd
        if cmd in (None, "help"):
            parser.print_help()
            return 0
        if cmd == "start":
            return cmd_start(opts)
        if cmd == "stop":
            return cmd_stop(opts)
        if cmd == "list":
            return cmd_list(opts)
        if cmd == "log":
            return cmd_log(opts)
        if cmd == "run":
            return cmd_run(opts)

        if cmd == "screen":
            return print_result(call(name, "screen"), raw)
        if cmd == "state":
            return print_result(call(name, "state"), raw, None if raw else format_state)
        if cmd == "ui":
            return print_result(call(name, "ui", {"all": opts.all}), raw, None if raw else format_ui)
        if cmd == "text":
            return print_result(call(name, "texts"), raw, None if raw else format_text)
        if cmd == "audit":
            return print_result(call(name, "layoutProblems"), raw, None if raw else (lambda r: print("\n".join(r or []) or "no layout problems")))
        if cmd == "native":
            return print_result(call(name, "nativeUi"), raw, None if raw else (lambda r: print(json.dumps(r, indent=1))))
        if cmd == "uimode":
            return print_result(call(name, "setUiMode", opts.key, opts.mode), raw)
        if cmd == "uiscale":
            return print_result(call(name, "setUiScale", opts.scale), raw)
        if cmd == "vm":
            return print_result(call(name, "viewModel", opts.name), raw, None if raw else (lambda r: print(json.dumps(r, indent=1, ensure_ascii=False))))
        if cmd in ("click", "rclick", "dblclick", "hover", "wait-for", "find"):
            fmt = None if raw else (lambda r: print(r["description"] if r else "not found"))
            return print_result(call(name, opts.fn, *locator_args(opts)), raw, fmt)
        if cmd == "wait-gone":
            return print_result(call(name, "waitGone", *locator_args(opts)), raw)
        if cmd == "key":
            return print_result(call(name, "key", opts.combo, opts.times), raw)
        if cmd == "type":
            return print_result(call(name, "type", opts.text), raw)
        if cmd == "move":
            return print_result(call(name, "move", opts.x, opts.y), raw)
        if cmd == "drag":
            return print_result(call(name, "drag", opts.x0, opts.y0, opts.x1, opts.y1), raw)
        if cmd == "wheel":
            return print_result(call(name, "wheel", opts.dy), raw)
        if cmd == "step":
            return print_result(call(name, "step", opts.frames), raw)
        if cmd == "wait":
            return print_result(call(name, "wait", opts.ms), raw)
        if cmd == "wait-idle":
            return print_result(call(name, "waitIdle", *([opts.ms] if opts.ms else [])), raw)
        if cmd == "wait-screen":
            return print_result(call(name, "waitScreen", opts.name, *([opts.ms] if opts.ms else [])), raw)
        if cmd == "shot":
            path = opts.path and str(Path(opts.path).resolve())
            if not path:
                reply = request(name, {"lua": "return ja2.screenshot('shot-' .. ja2.frame() .. '.png')"})
            else:
                reply = call(name, "screenshot", path)
            return print_result(reply, raw)
        if cmd == "pixel":
            return print_result(call(name, "pixel", opts.x, opts.y), raw)
        if cmd == "saves":
            return print_result(call(name, "saves"), raw, None if raw else (lambda r: print("\n".join(r or []))))
        if cmd == "load":
            return print_result(call(name, "load", opts.name), raw)
        if cmd == "save":
            return print_result(call(name, "save", opts.name, opts.description or opts.name), raw)
        if cmd == "eval":
            return print_result(request(name, {"lua": opts.code}), raw)
    except SessionError as e:
        print(f"ja2ctl: {e}", file=sys.stderr)
        return 2
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
