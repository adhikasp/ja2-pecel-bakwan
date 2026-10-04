#!/usr/bin/env python3
"""dev.py - one entry point for building, testing and running ja2-pecel-bakwan.

The same commands work on macOS and Windows (on Windows this shells into the
MSYS2 MinGW64 environment itself, so no more `MSYSTEM=MINGW64 bash -lc ...`):

    python tools/dev.py setup       # make this worktree ready (idempotent)
    python tools/dev.py build       # incremental build
    python tools/dev.py test        # C++ unit tests
    python tools/dev.py e2e [args]  # end-to-end tests
    python tools/dev.py run [args]  # play the game

`setup` configures the build directory (`_bin` on Windows, `build` elsewhere)
with Ninja, sccache and lld when the machine has them, runs `uv sync` and
checks `game_dir`. It is safe to run any number of times.

Worktrees build side by side. Each builds in its own directory at its own
path, and `SCCACHE_BASEDIR` points the compiler cache at that root, so the
absolute path stops being part of the cache key: a build of master warms every
other worktree of the same code even though they sit at different paths.
(That used to be done with one machine-wide `subst` drive under an exclusive
lock, which serialised every agent on the machine behind one build path; see
"Cache sharing" in COMPILATION.md.) `build` and `e2e` still cap parallel jobs
through a machine-wide semaphore (override with JA2_JOBS or --jobs) and one
process builds a build directory at a time.

`bootstrap` is "setup + build". Agent sessions run `python tools/dev.py
bootstrap --auto` on start (wired up in `.opencode/plugins/dev-bootstrap/` and
`.claude/settings.json`), which bootstraps the worktree in the background so a
fresh checkout is buildable and testable without anyone asking.

Run `python tools/dev.py --help` for all commands.
"""

import argparse
import contextlib
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
IS_WINDOWS = os.name == "nt"
BUILD_DIR_NAME = "_bin" if IS_WINDOWS else "build"
MSYS2_ROOT = Path(os.environ.get("MSYS2_ROOT", r"C:\msys64"))
JA2_EXE = "ja2" + (".exe" if IS_WINDOWS else "")
TOOL_WHICH = {}
# sccache's default cache ceiling is 10G, but one full build of this tree stores
# several GB of objects plus preprocessed sources, and every worktree/branch adds
# its own set. At 10G the LRU starts evicting and unchanged code rebuilds cold,
# which is the single biggest cause of multi-minute builds. The disk has hundreds
# of GB free, so let the cache be large enough to actually be a cache. Override
# with SCCACHE_CACHE_SIZE in the environment.
SCCACHE_CACHE_SIZE = "50G"


# --- small helpers ----------------------------------------------------------

def log(msg: str):
    print(f"dev.py: {msg}", flush=True)


def warn(msg: str):
    print(f"dev.py: warning: {msg}", flush=True)


def die(msg: str, code: int = 1):
    print(f"dev.py: error: {msg}", flush=True)
    sys.exit(code)


def fmt_secs(s: float) -> str:
    return f"{int(s) // 60}m{int(s) % 60:02d}s" if s >= 60 else f"{s:.1f}s"


def user_home() -> Path:
    return Path(os.environ.get("USERPROFILE") or os.environ.get("HOME") or Path.home())


def state_dir() -> Path:
    """Machine-wide state: subst mappings, job semaphore, bootstrap logs."""
    if IS_WINDOWS:
        base = os.environ.get("LOCALAPPDATA") or (user_home() / "AppData" / "Local")
        return Path(base) / "ja2-dev"
    return user_home() / ".ja2-dev"


# --- the tool environment ---------------------------------------------------

def bash_exe() -> Path:
    bash = MSYS2_ROOT / "usr" / "bin" / "bash.exe"
    if IS_WINDOWS and not bash.exists():
        die(f"MSYS2 bash not found at {bash}; install MSYS2 or set MSYS2_ROOT")
    return bash


def posix(path: Path) -> str:
    """An MSYS2-style path (`W:/_bin` -> `/w/_bin`); plain paths pass through."""
    s = str(path)
    if not IS_WINDOWS:
        return s
    m = re.match(r"^([A-Za-z]):[\\/]", s)
    tail = s[m.end():].replace("\\", "/") if m else s.replace("\\", "/")
    return (f"/{m.group(1).lower()}/{tail}" if m else tail).rstrip("/") or "/"


def tool_run(cmd: str, cwd: Path, capture: bool = False):
    """Run a build-tool command inside the tool environment (MSYS2 MinGW64)."""
    # Always a dict we can add to: the non-Windows path used to pass env=None
    # (plain inheritance), which cannot carry the SCCACHE_BASEDIR below.
    env = dict(os.environ)
    if IS_WINDOWS:
        argv = [str(bash_exe()), "-lc", f"cd '{posix(cwd)}' && {cmd}"]
        env["MSYSTEM"] = "MINGW64"
        env.setdefault("SCCACHE_CACHE_SIZE", SCCACHE_CACHE_SIZE)
    else:
        argv = ["/bin/bash", "-lc", f"cd {shlex.quote(str(cwd))} && {cmd}"]
    # Share the compiler cache across worktrees without pinning them to one
    # build path: sccache rewrites paths under SCCACHE_BASEDIR to be relative
    # before hashing, so two checkouts at different absolute paths produce the
    # same key. Without this each worktree is its own cache island and the
    # second worktree on a branch recompiles the world. Both worktree roots and
    # the in-tree `_bin` are under REPO, so one value covers every source and
    # generated header in the compile.
    env["SCCACHE_BASEDIR"] = str(REPO)
    return subprocess.run(argv, env=env, capture_output=capture, text=True)


def tool_which(name: str):
    if name not in TOOL_WHICH:
        r = tool_run(f"command -v {shlex.quote(name)}", REPO, capture=True)
        TOOL_WHICH[name] = r.stdout.strip() if r.returncode == 0 else None
    return TOOL_WHICH[name]


def binary_env() -> dict:
    """Environment for running built binaries (MinGW runtime DLLs on Windows)."""
    env = dict(os.environ)
    if IS_WINDOWS:
        mingw = MSYS2_ROOT / "mingw64" / "bin"
        if mingw.exists():
            env["PATH"] = str(mingw) + os.pathsep + env.get("PATH", "")
    return env


def find_uv():
    uv = shutil.which("uv")
    if uv:
        return uv
    for base in {user_home(), Path(os.environ.get("USERPROFILE", "") or "/")}:
        for name in ("uv.exe", "uv"):
            candidate = base / ".local" / "bin" / name
            if candidate.exists():
                return str(candidate)
    return None


# --- sharing the machine with other agents ----------------------------------

def pid_alive(pid: int) -> bool:
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if not IS_WINDOWS:
        try:
            os.kill(pid, 0)
        except OSError:
            return False
        return True
    import ctypes
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    kernel32.CloseHandle(handle)
    return True


@contextlib.contextmanager
def job_slots(kind: str, requested=None):
    """Cap parallel jobs across every agent on this machine.

    Each concurrent build/e2e registers in a machine-wide directory and gets
    `cpu_count / active` jobs, so N agents do not each grab every core.
    JA2_JOBS or --jobs overrides the computed count outright.
    """
    registry = state_dir() / "active"
    registry.mkdir(parents=True, exist_ok=True)
    for stale in registry.glob("*.json"):
        try:
            rec = json.loads(stale.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            stale.unlink(missing_ok=True)
            continue
        if not pid_alive(rec.get("pid")) or time.time() - rec.get("started", 0) > 6 * 3600:
            stale.unlink(missing_ok=True)
    active = len(list(registry.glob("*.json"))) + 1
    entry = registry / f"{kind}-{os.getpid()}.json"
    entry.write_text(json.dumps({"pid": os.getpid(), "kind": kind, "started": time.time()}), encoding="utf-8")
    try:
        env_jobs = int(os.environ.get("JA2_JOBS") or 0)
    except ValueError:
        env_jobs = 0
    jobs = env_jobs or requested or max(1, (os.cpu_count() or 4) // active)
    try:
        yield jobs
    finally:
        entry.unlink(missing_ok=True)


@contextlib.contextmanager
def build_lock(build_dir: Path):
    """One builder per build directory (ninja/make are not safe to run twice)."""
    lock = build_dir / ".dev-build.lock"
    owner = json.dumps({"pid": os.getpid(), "started": time.time()})
    waiting_since = None
    while True:
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, owner.encode())
            os.close(fd)
            break
        except FileExistsError:
            try:
                pid = json.loads(lock.read_text(encoding="utf-8")).get("pid")
            except (OSError, ValueError):
                pid = None
            if not pid_alive(pid):
                lock.unlink(missing_ok=True)
                continue
            if waiting_since is None:
                waiting_since = time.time()
                log(f"waiting for the other build in this worktree (pid {pid}) ...")
            elif time.time() - waiting_since > 30:
                waiting_since = time.time()
                log(f"still waiting for pid {pid} to finish building ...")
            time.sleep(2)
    try:
        yield
    finally:
        lock.unlink(missing_ok=True)


# --- setup ------------------------------------------------------------------

def venv_python():
    for candidate in (REPO / ".venv" / "Scripts" / "python.exe", REPO / ".venv" / "bin" / "python"):
        if candidate.exists():
            return candidate
    return None


def sync_venv(quiet: bool = False):
    """`uv sync`: Pillow + numpy for the golden-image tests (see pyproject.toml)."""
    marker = REPO / ".venv" / ".ja2-dev-synced"
    deps = [REPO / "pyproject.toml", REPO / "uv.lock"]
    if marker.exists() and all(p.exists() and p.stat().st_mtime <= marker.stat().st_mtime for p in deps):
        return
    uv = find_uv()
    if not uv:
        warn("uv not found; skipping `uv sync` (the golden-image tests need it: https://docs.astral.sh/uv/)")
        return
    if not quiet:
        log("uv sync")
    if subprocess.run([uv, "sync"], cwd=str(REPO)).returncode != 0:
        warn("`uv sync` failed; golden-image tests will not have Pillow/numpy")
        return
    venv = REPO / ".venv"
    if venv.exists():
        marker.write_text(str(time.time()), encoding="utf-8")


def ja2_config_path() -> Path:
    if IS_WINDOWS:
        appdata = os.environ.get("APPDATA")
        base = Path(appdata) if appdata else user_home() / "AppData" / "Roaming"
        return base / "JA2" / "ja2.json"
    return user_home() / ".ja2" / "ja2.json"


def check_game_dir(quiet: bool = False):
    """The e2e harness needs the original game data; point out the classic trap."""
    config = ja2_config_path()
    game_dir = None
    try:
        game_dir = json.loads(config.read_text(encoding="utf-8")).get("game_dir")
    except (OSError, ValueError):
        pass
    if not game_dir:
        warn(f"game_dir is not set in {config}; e2e tests need the original game data")
        if IS_WINDOWS:
            warn("the Steam default is: C:/Program Files (x86)/Steam/steamapps/common/Jagged Alliance 2 Gold")
        return
    root = Path(game_dir)
    if root.name.lower() == "data" or not (root / "Data").is_dir():
        warn(f"game_dir in {config} is {game_dir}")
        warn("game_dir must point at the install root (the folder containing Data\\), not at the Data folder"
             " itself - pointing it at Data makes the VFS look for a nonexistent Data\\data and fail with"
             ' "Error initializing VFS ... os error 3"')
        return
    if not quiet:
        log(f"game_dir: {game_dir}")


def is_configured(build_dir: Path) -> bool:
    return (build_dir / "CMakeCache.txt").exists()


def generator_of(build_dir: Path):
    try:
        for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_GENERATOR:"):
                return line.split("=", 1)[1]
    except OSError:
        pass
    return None


def cached_option(build_dir: Path, name: str):
    """What cmake already cached for `name` here, or None if it never ran."""
    try:
        for line in (build_dir / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(name + ":"):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None


def build_sdl_from_source(build_dir: Path) -> bool:
    """Whether to configure with `-DBUILD_SDL_LIB=ON` (fetch and build SDL3).

    Windows and macOS get a prebuilt SDL3 out of `dependencies/`, named by their
    `cmake/toolchain-*.cmake`. Linux ships none: CI installs one with the
    libsdl-org/setup-sdl action, so a plain Linux box - a fresh VM, a container,
    an ARM cloud node - has no SDL3 and `find_package(SDL3)` fails the configure.
    The repo can build it instead (`BUILD_SDL_LIB`, which is how the Android build
    gets SDL), so do that rather than stopping: it is the same pinned 3.4.16 the
    other platforms use.

    An SDL3 that pkg-config already knows about is used as-is, and a build
    directory that already has an opinion (the cache holds BUILD_SDL_LIB) keeps
    it - so installing SDL3 later, or configuring with -DBUILD_SDL_LIB=OFF by
    hand, still wins.
    """
    if IS_WINDOWS or sys.platform == "darwin":
        return False
    if cached_option(build_dir, "BUILD_SDL_LIB") is not None:
        return False
    return tool_run("pkg-config --exists sdl3", REPO).returncode != 0


def configure(root: Path, build_dir: Path, quiet: bool = False):
    build_dir.mkdir(parents=True, exist_ok=True)
    ninja = tool_which("ninja")
    cmd = f"cmake -S '{posix(root)}' -B '{posix(build_dir)}'"
    if ninja:
        cmd += " -G Ninja"
    if build_sdl_from_source(build_dir):
        cmd += " -DBUILD_SDL_LIB=ON"
        if not quiet:
            log("no SDL3 installed; building the pinned one from source (-DBUILD_SDL_LIB=ON)")
    if not quiet:
        log(f"configure {build_dir} ({'Ninja' if ninja else 'default generator'}"
            f"{', sccache' if tool_which('sccache') else ''}{', lld' if tool_which('ld.lld') else ''})")
    with build_lock(build_dir):
        if tool_run(cmd, build_dir).returncode != 0:
            die("cmake configure failed; see the output above")
        stamp = build_dir / ".dev-setup.json"
        stamp.write_text(json.dumps({"generator": "Ninja" if ninja else "default",
                                     "venv": venv_python() is not None,
                                     "root": str(root), "time": time.time()}), encoding="utf-8")


def stamp_stale(build_dir: Path) -> bool:
    """Reconfigure when the venv appeared after the last configure (ctest finds it)."""
    try:
        stamp = json.loads((build_dir / ".dev-setup.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return True
    return bool(stamp.get("venv")) != (venv_python() is not None)


def configured_root(build_dir: Path):
    try:
        return json.loads((build_dir / ".dev-setup.json").read_text(encoding="utf-8")).get("root")
    except (OSError, ValueError):
        return None


def setup(root: Path, build_dir: Path, quiet: bool = False):
    check_game_dir(quiet)
    sync_venv(quiet)
    if is_configured(build_dir) and configured_root(build_dir) != str(root):
        # the build directory was configured for a different source path (e.g.
        # it used to be reached through the machine-wide subst drive): every
        # path baked into the build files would change, so start it over
        if not quiet:
            log(f"{build_dir} was configured for {configured_root(build_dir)}; reconfiguring for {root}")
        shutil.rmtree(build_dir, ignore_errors=True)
    if is_configured(build_dir) and not stamp_stale(build_dir):
        if not quiet:
            log(f"{build_dir} already configured ({generator_of(build_dir) or 'unknown generator'})")
        return
    configure(root, build_dir, quiet)


def setup_if_needed(root: Path, build_dir: Path):
    if not is_configured(build_dir) or stamp_stale(build_dir) or configured_root(build_dir) != str(root):
        setup(root, build_dir, quiet=True)


# --- commands ---------------------------------------------------------------

def build_binary(root: Path, build_dir: Path, jobs, target=None) -> int:
    with build_lock(build_dir), job_slots("build", jobs) as j:
        cmd = f"cmake --build '{posix(build_dir)}' --parallel {j}"
        if target:
            cmd += f" --target {shlex.quote(target)}"
        log(f"building in {build_dir} ({j} job{'s' if j != 1 else ''})")
        started = time.time()
        rc = tool_run(cmd, build_dir).returncode
        log(f"{'built' if rc == 0 else 'build failed'} in {fmt_secs(time.time() - started)}")
        return rc


def cmd_setup(args):
    build_dir = REPO / BUILD_DIR_NAME
    setup(REPO, build_dir, quiet=args.quiet)
    log(f"ready: {build_dir} (run: python tools/dev.py build)")


def cmd_build(args):
    build_dir = REPO / BUILD_DIR_NAME
    setup_if_needed(REPO, build_dir)
    sys.exit(build_binary(REPO, build_dir, args.jobs, args.target))


def cmd_test(args):
    build_dir = REPO / BUILD_DIR_NAME
    setup_if_needed(REPO, build_dir)
    if not args.no_build:
        rc = build_binary(REPO, build_dir, args.jobs)
        if rc:
            sys.exit(rc)
    binary = build_dir / JA2_EXE
    if not binary.exists():
        die(f"{binary} does not exist; run: python tools/dev.py build")
    argv = [str(binary), "-unittests"] + args.args
    log(" ".join(shlex.quote(a) for a in argv))
    sys.exit(subprocess.run(argv, cwd=str(build_dir), env=binary_env()).returncode)


def cmd_e2e(args):
    build_dir = REPO / BUILD_DIR_NAME
    setup_if_needed(REPO, build_dir)
    if not args.no_build:
        rc = build_binary(REPO, build_dir, args.jobs)
        if rc:
            sys.exit(rc)
    if args.args and (args.args[0].endswith((".lua", ".txt")) or (REPO / args.args[0]).exists()):
        # one script: tools/ja2ctl.py run <script> [--isolated] [--show] ...
        argv = [sys.executable, str(REPO / "tools" / "ja2ctl.py"), "run"] + args.args
        log(" ".join(shlex.quote(a) for a in argv))
        sys.exit(subprocess.run(argv, cwd=str(REPO), env=binary_env()).returncode)
    with job_slots("e2e", args.jobs) as jobs:
        cmd = f"ctest -L e2e --output-on-failure --parallel {jobs}"
        if args.args:
            cmd += " " + " ".join(shlex.quote(a) for a in args.args)
        log(cmd)
        sys.exit(tool_run(cmd, build_dir).returncode)


def cmd_run(args):
    build_dir = REPO / BUILD_DIR_NAME
    setup_if_needed(REPO, build_dir)
    if not args.no_build:
        rc = build_binary(REPO, build_dir, args.jobs)
        if rc:
            sys.exit(rc)
    binary = build_dir / JA2_EXE  # the game may run for hours
    if not binary.exists():
        die(f"{binary} does not exist; run: python tools/dev.py build")
    default_res = [] if "-res" in args.args else ["-res", "1280x720"]
    argv = [str(binary)] + default_res + args.args
    log(" ".join(shlex.quote(a) for a in argv))
    sys.exit(subprocess.run(argv, cwd=str(build_dir), env=binary_env()).returncode)


def bootstrap_auto():
    """Hook mode: never block the session, never double up, quiet on success."""
    state = state_dir()
    state.mkdir(parents=True, exist_ok=True)
    db = state / "bootstrap.json"
    try:
        stamps = json.loads(db.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        stamps = {}
    key = str(REPO)
    if time.time() - stamps.get(key, {}).get("time", 0) < 120:
        return  # this worktree was just bootstrapped (or is being, right now)
    digest = hashlib.sha1(key.encode()).hexdigest()[:8]
    logs = state / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_file = logs / f"bootstrap-{REPO.name}-{digest}-{stamp}.log"
    for stale in sorted(logs.glob(f"bootstrap-{REPO.name}-{digest}-*.log"))[:-4]:
        stale.unlink(missing_ok=True)  # keep the newest few, one file per run so they never interleave
    stamps[key] = {"time": time.time(), "log": str(log_file)}
    db.write_text(json.dumps(stamps, indent=2), encoding="utf-8")
    argv = [sys.executable, str(Path(__file__).resolve()), "bootstrap"]
    flags = {"creationflags": 0x00000008 | 0x00000200} if IS_WINDOWS else {"start_new_session": True}  # DETACHED_PROCESS
    try:
        with open(log_file, "ab") as out:
            subprocess.Popen(argv, cwd=str(REPO), stdin=subprocess.DEVNULL, stdout=out,
                             stderr=subprocess.STDOUT, **flags)
    except OSError as exc:
        warn(f"could not start the background bootstrap: {exc}")
        return
    log(f"bootstrapping {REPO.name} in the background (log: {log_file})")


def cmd_bootstrap(args):
    if args.auto:
        bootstrap_auto()
        return
    build_dir = REPO / BUILD_DIR_NAME
    setup(REPO, build_dir)
    sys.exit(build_binary(REPO, build_dir, args.jobs))


def cmd_status(args):
    build_dir = REPO / BUILD_DIR_NAME
    print(f"worktree:   {REPO}")
    print(f"build dir:  {build_dir}"
          + (f"  [{generator_of(build_dir)}]" if is_configured(build_dir) else "  [not configured yet]"))
    binary = build_dir / JA2_EXE
    if binary.exists():
        age = time.strftime("%Y-%m-%d %H:%M", time.localtime(binary.stat().st_mtime))
        print(f"binary:     {binary}  (built {age})")
    else:
        print(f"binary:     {binary}  (not built yet)")
    print(f"venv:       {venv_python() or 'not created (run: python tools/dev.py setup)'}")
    print(f"config:     {ja2_config_path()}")
    for name in ("cmake", "ninja", "sccache", "ld.lld", "cargo", "rustc"):
        print(f"{name + ':':<12}{tool_which(name) or 'not found'}")
    uv = find_uv()
    print(f"{'uv:':<12}{uv or 'not found'}")
    check_game_dir()
    active = list((state_dir() / "active").glob("*.json"))
    print(f"{'active:':<12}{len(active)} build/test process(es) on this machine")
    print(f"{'cache:':<12}SCCACHE_BASEDIR={REPO} (shared with every worktree)")


# --- command line -----------------------------------------------------------

# dev.py's own flags for `test`, `e2e` and `run`; anything else is passed on to
# the test binary, ctest, ja2ctl or ja2. Use `--` to hand over everything.
DEV_FLAGS = ("-h", "--help", "--no-build", "-j", "--jobs", "--jobs=")


def split_tail(tail):
    """(dev.py tokens, child args) for the passthrough commands."""
    dev, child = [], []
    i = 0
    while i < len(tail):
        arg = tail[i]
        if arg == "--":
            child += tail[i + 1:]
            break
        if arg in ("-j", "--jobs") and i + 1 < len(tail):
            dev += tail[i:i + 2]
            i += 2
            continue
        if arg in DEV_FLAGS or arg.startswith("--jobs=") or re.fullmatch(r"-j\d+", arg):
            dev.append(arg)
        else:
            child.append(arg)
        i += 1
    return dev, child


def main():
    parser = argparse.ArgumentParser(
        prog="tools/dev.py",
        description="one entry point for building, testing and running ja2-pecel-bakwan (macOS and Windows)",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    def common(p):
        p.add_argument("-j", "--jobs", type=int, default=None,
                       help="parallel jobs (default: share this machine with the other agents; JA2_JOBS overrides)")
        p.add_argument("--no-build", action="store_true", help="do not build first")

    p = sub.add_parser("setup", help="make this worktree ready: build dir, uv sync, game_dir (idempotent)")
    p.add_argument("-q", "--quiet", action="store_true", help="only report problems")
    p.set_defaults(func=cmd_setup)

    p = sub.add_parser("build", help="build the game (job-capped across agents)")
    p.add_argument("-j", "--jobs", type=int, default=None,
                   help="parallel jobs (default: share this machine with the other agents; JA2_JOBS overrides)")
    p.add_argument("--target", help="build one cmake target")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("test", help="build and run the C++ unit tests (extra args go to the test binary)")
    common(p)
    p.set_defaults(func=cmd_test)

    p = sub.add_parser("e2e", help="build and run the e2e tests: ctest -L e2e, or one ja2ctl script")
    common(p)
    p.set_defaults(func=cmd_e2e)

    p = sub.add_parser("run", help="build and play the game (default -res 1280x720)")
    common(p)
    p.set_defaults(func=cmd_run)

    p = sub.add_parser("bootstrap", help="setup + build: make this worktree ready to test")
    p.add_argument("-j", "--jobs", type=int, default=None, help="parallel jobs (JA2_JOBS overrides)")
    p.add_argument("--auto", action="store_true",
                   help="session-hook mode: bootstrap in the background, return immediately")
    p.set_defaults(func=cmd_bootstrap)

    p = sub.add_parser("status", help="show what dev.py sees: paths, tools, game_dir, build state")
    p.set_defaults(func=cmd_status)

    argv = sys.argv[1:]
    if argv and argv[0] in {"test", "e2e", "run"}:
        dev, child = split_tail(argv[1:])
        args = parser.parse_args([argv[0]] + dev)
        args.args = child
    else:
        args = parser.parse_args(argv)
    try:
        args.func(args)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
