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
path, and `SCCACHE_BASEDIRS` points the compiler cache at every checkout of the
repo on the machine, so the absolute path stops being part of the cache key:
a build of master warms every other worktree of the same code even though they
sit at different paths.
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
import functools
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
# sccache reads its base directories once, when the server starts, and it reads
# them from SCCACHE_BASEDIRS - plural, and a list. The singular SCCACHE_BASEDIR
# that sccache used before 0.16 is ignored without a word, which is worse than
# an error: the build succeeds, the cache fills, and no two checkouts ever share
# an entry. That is why the name is asserted in tools/tests/test_sccache.py, and
# why the server is checked rather than assumed in sync_sccache_server().
SCCACHE_BASEDIRS = "SCCACHE_BASEDIRS"


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


def tool_env() -> dict:
    """The environment a build tool runs in: MSYS2's, plus the cache settings."""
    # Always a dict we can add to: the non-Windows path used to pass env=None
    # (plain inheritance), which cannot carry the base directories below.
    env = dict(os.environ)
    if IS_WINDOWS:
        env["MSYSTEM"] = "MINGW64"
        env.setdefault("SCCACHE_CACHE_SIZE", SCCACHE_CACHE_SIZE)
    # Share the compiler cache across checkouts without pinning them to one
    # build path: sccache rewrites paths under these directories to be relative
    # before hashing, so two checkouts at different absolute paths produce the
    # same key. Without it each worktree is its own cache island and the second
    # worktree on a branch recompiles the world.
    env[SCCACHE_BASEDIRS] = sccache_basedirs()
    return env


def tool_run(cmd: str, cwd: Path, capture: bool = False):
    """Run a build-tool command inside the tool environment (MSYS2 MinGW64)."""
    if IS_WINDOWS:
        argv = [str(bash_exe()), "-lc", f"cd '{posix(cwd)}' && {cmd}"]
    else:
        argv = ["/bin/bash", "-lc", f"cd {shlex.quote(str(cwd))} && {cmd}"]
    return subprocess.run(argv, env=tool_env(), capture_output=capture, text=True)


@functools.lru_cache(maxsize=1)
def worktree_roots():
    """Every checkout of this repo on this machine, this one first.

    The base directories have to cover all of them at once. One sccache server
    serves the whole machine, and it strips only the directories it was started
    with, so a build in the main checkout has to be a cache hit in an agent
    worktree and the other way round - and those two sit at different absolute
    paths by design. Cached, because this is asked for by every tool command and
    a new worktree appearing mid-build can wait for the next one.
    """
    roots = [REPO]
    try:
        r = subprocess.run(["git", "-C", str(REPO), "worktree", "list", "--porcelain"],
                           capture_output=True, text=True)
    except OSError:
        return roots
    if r.returncode == 0:
        for p in parse_worktree_list(r.stdout):
            if p not in roots:
                roots.append(p)
    return roots


MSYS_DRIVE = re.compile(r"^/([A-Za-z])/(.*)$")


def native_root(path) -> str:
    """One path spelled the way the platform does, from whatever git printed.

    Git is not one program on Windows. Run `dev.py` from MSYS2's shell and `git`
    is MSYS2's, which spells a Windows path `/c/Users/...`; run it from cmd and
    `git` is Git for Windows, which spells it `C:/Users/...`. Both have to come
    out as the absolute Windows path sccache insists on - it refuses to start on
    anything else, and a server that cannot start is every compile in the build
    failing.
    """
    s = str(path).replace("\\", "/")
    if IS_WINDOWS:
        m = MSYS_DRIVE.match(s)
        if m:
            s = f"{m.group(1).upper()}:/{m.group(2)}"
    return s


def parse_worktree_list(porcelain: str):
    """The absolute checkout roots in `git worktree list --porcelain` output.

    Anything that is not absolute is dropped rather than passed on: a base
    directory sccache rejects takes the whole build with it, and losing one root
    only costs a cache miss in that checkout.
    """
    roots = []
    for line in porcelain.splitlines():
        if line.startswith("worktree "):
            p = Path(native_root(line[len("worktree "):].strip()))
            if p.is_absolute() and p not in roots:
                roots.append(p)
    return roots


def sccache_basedirs() -> str:
    """The SCCACHE_BASEDIRS value: every checkout root, joined the way sccache wants."""
    return os.pathsep.join(str(p) for p in worktree_roots())


def normalize_root(path) -> str:
    """A root the way sccache reports it: forward slashes, lower case, trailing '/'.

    Windows matching is case-insensitive and the server normalizes what it
    echoes back, so comparing the reported list with ours has to normalize too.
    """
    return str(path).strip().replace("\\", "/").rstrip("/").lower() + "/"


def sccache_reported_basedirs():
    """What the running sccache server has for base directories, or None.

    `--show-stats` is the only way to ask, and it starts a server (with the
    environment it is given) when none is running - so this is the server's
    configuration in both cases, which is the point: the server keeps what it
    was started with. None means "could not tell" and the caller must then fall
    back on what it knows rather than restart on a hunch.
    """
    r = tool_run("sccache --show-stats", REPO, capture=True)
    if r.returncode != 0:
        return None
    for line in (r.stdout or "").splitlines():
        if line.strip().startswith("Base directories"):
            tail = line.split("Base directories", 1)[1].strip()
            if tail in ("", "(none)"):
                return set()
            return {normalize_root(p) for p in tail.split(",") if p.strip()}
    return None


SCCACHE_SYNCED = False


def sync_sccache_server():
    """Make sure the running server was started with our base directories.

    sccache applies its configuration when the server starts and a server that
    is already up keeps the directories it was given, so one started before this
    machine knew about a checkout quietly goes back to hashing absolute paths -
    the failure the base directories exist to prevent, and an invisible one.

    Two signals, because neither is enough alone: what the server reports, and
    what the last build on this machine asked for. Asking starts a server when
    none is running (with our environment, so with the right directories); one
    that does not know about every checkout we are about to build in is stopped,
    and the next compile starts a server that does.

    Once per process: the set of checkouts cannot change under a single build,
    and this runs from both the configure and the build path.
    """
    global SCCACHE_SYNCED
    if SCCACHE_SYNCED or not tool_which("sccache"):
        return
    SCCACHE_SYNCED = True
    want = {normalize_root(p) for p in worktree_roots()}
    basedirs = sccache_basedirs()
    marker = state_dir() / "sccache-basedirs.txt"
    try:
        last = marker.read_text(encoding="utf-8").strip()
    except OSError:
        last = None
    have = sccache_reported_basedirs()
    stale = last != basedirs or (have is not None and not have >= want)
    if not stale:
        return
    log("restarting the sccache server so every checkout shares one cache")
    tool_run("sccache --stop-server", REPO, capture=True)
    try:
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(basedirs, encoding="utf-8")
    except OSError:
        pass




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
def file_lock(path: Path, what: str = ""):
    """One holder at a time for a named file; a dead holder's lock expires.

    Same shape as the build lock, generalised: anything that mutates a shared
    resource several agents can reach at once (a build directory, the
    pr-screenshots worktree) takes this instead of a machine-wide semaphore,
    because the conflict is about *which* resource, not how much CPU is free.
    """
    what = what or path.name
    path.parent.mkdir(parents=True, exist_ok=True)
    owner = json.dumps({"pid": os.getpid(), "started": time.time()})
    waiting_since = None
    while True:
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, owner.encode())
            os.close(fd)
            break
        except FileExistsError:
            try:
                pid = json.loads(path.read_text(encoding="utf-8")).get("pid")
            except (OSError, ValueError):
                pid = None
            if not pid_alive(pid):
                path.unlink(missing_ok=True)
                continue
            if waiting_since is None:
                waiting_since = time.time()
                log(f"waiting for the other {what} (pid {pid}) ...")
            elif time.time() - waiting_since > 30:
                waiting_since = time.time()
                log(f"still waiting for pid {pid} to finish {what} ...")
            time.sleep(2)
    try:
        yield
    finally:
        path.unlink(missing_ok=True)


def build_lock(build_dir: Path):
    """One builder per build directory (ninja/make are not safe to run twice)."""
    return file_lock(build_dir / ".dev-build.lock", "build in this worktree")


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
        sync_sccache_server()
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
        sync_sccache_server()
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


@functools.lru_cache(maxsize=None)
def ctest_supports_junit(build_dir: Path) -> bool:
    """Whether this ctest can write a JUnit report (`--output-junit`, CMake 3.21+).

    `cmake_minimum_required` is 3.18.1, so it cannot be assumed; a plain
    `ctest --help` is the cheap, version-independent probe, and the report is a
    diagnostic that must never break a run on an older ctest.
    """
    r = tool_run("ctest --help", build_dir, capture=True)
    return "--output-junit" in (r.stdout or "")


def ctest_json_runner(select, build_dir: Path):
    """A `run(argv, cwd)` for e2e_select that goes through the tool environment.

    `ctest` is not on the PATH of the Python process on Windows (it lives in
    MSYS2), so the selector's default subprocess runner is replaced with one
    that enters MSYS2 the way every other dev.py command does.
    """
    def run(argv, cwd):
        r = tool_run(" ".join(shlex.quote(a) for a in argv), build_dir, capture=True)
        if r.returncode != 0:
            raise select.CTestError(f"{' '.join(argv)} failed: {(r.stderr or r.stdout).strip()}")
        return r.stdout
    return run


def changed_selection(build_dir: Path, base: str) -> str:
    """`--changed`: the `ctest -R` regex for the areas a diff can affect, or "".

    "" means run everything: either a changed path has no area mapped (the safe
    direction), or the diff only touched files that cannot affect a run. The
    smoke tier is always part of a selection, so a narrowed run is never empty.
    """
    select = tool_module("e2e_select")
    try:
        paths = select.changed_paths(REPO, base)
        test_areas = select.ctest_test_areas(build_dir, run=ctest_json_runner(select, build_dir))
    except select.SelectError as exc:
        die(str(exc))
    selection = select.select(paths, test_areas)
    if selection.fallback:
        log(f"--changed: {len(paths)} changed file(s); one has no area mapped, running every e2e test")
        return ""
    log(f"--changed: {len(paths)} changed file(s) -> areas: "
        f"{', '.join(sorted(selection.areas)) or '(none)'}")
    log(f"--changed: {len(selection.tests)} test(s): {' '.join(selection.tests)}")
    return select.test_regex(selection.tests)


def print_slowest(report: Path):
    """The slowest e2e tests from the run's JUnit report, so regressions show."""
    select = tool_module("e2e_select")
    try:
        entries = select.parse_junit(report.read_text(encoding="utf-8"))
    except (OSError, select.SelectError) as exc:
        warn(f"no slowest-test report: {exc}")
        return
    for line in select.report_lines(entries):
        log(line)


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
    # The slowest-test report needs ctest's own timings; ask ctest for them
    # (`--output-junit`) so a runtime regression is visible after every run.
    report = build_dir / "e2e-timings.xml"
    junit = ctest_supports_junit(build_dir)
    if junit:
        report.unlink(missing_ok=True)  # a stale file must not be read as this run's
    selected = changed_selection(build_dir, args.changed) if args.changed is not None else ""
    with job_slots("e2e", args.jobs) as jobs:
        cmd = "ctest -L e2e"
        if selected:
            cmd += " -R " + shlex.quote(selected)
        cmd += f" --output-on-failure --parallel {jobs}"
        if junit:
            cmd += " --output-junit " + shlex.quote(str(report))
        if args.args:
            cmd += " " + " ".join(shlex.quote(a) for a in args.args)
        log(cmd)
        rc = tool_run(cmd, build_dir).returncode
    if junit:
        print_slowest(report)
    sys.exit(rc)


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


def source_fingerprint():
    """HEAD plus the state of every uncommitted file; None when git cannot say.

    Equal fingerprints mean the tree is what the last successful bootstrap built,
    so the session hook can skip the whole cmake/ninja/cargo pass.
    """
    def git(*argv):
        r = subprocess.run(["git", "-C", str(REPO), *argv], capture_output=True, text=True)
        return r.stdout if r.returncode == 0 else None

    head = git("rev-parse", "HEAD")
    status = git("status", "--porcelain", "-z", "--untracked-files=all")
    if head is None or status is None:
        return None
    h = hashlib.sha1(head.encode())
    for entry in sorted(e for e in status.split("\0") if len(e) > 3):
        h.update(entry.encode())
        try:
            st = (REPO / entry[3:]).stat()
            h.update(f"{st.st_mtime_ns}:{st.st_size}".encode())
        except OSError:
            pass  # deleted or a rename's old path: the status entry already says so
    return h.hexdigest()


def read_bootstrap_db(db: Path) -> dict:
    try:
        return json.loads(db.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def bootstrap_auto():
    """Hook mode: never block the session, never double up, quiet on success."""
    state = state_dir()
    state.mkdir(parents=True, exist_ok=True)
    db = state / "bootstrap.json"
    stamps = read_bootstrap_db(db)
    key = str(REPO)
    entry = stamps.get(key, {})
    if time.time() - entry.get("time", 0) < 120:
        return  # this worktree was just bootstrapped (or is being, right now)
    fingerprint = source_fingerprint()
    if fingerprint and entry.get("built") == fingerprint and (REPO / BUILD_DIR_NAME / "CMakeCache.txt").exists():
        return  # nothing changed since the last successful bootstrap: no build, no process spawns
    digest = hashlib.sha1(key.encode()).hexdigest()[:8]
    logs = state / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_file = logs / f"bootstrap-{REPO.name}-{digest}-{stamp}.log"
    for stale in sorted(logs.glob(f"bootstrap-{REPO.name}-{digest}-*.log"))[:-4]:
        stale.unlink(missing_ok=True)  # keep the newest few, one file per run so they never interleave
    stamps[key] = {**entry, "time": time.time(), "log": str(log_file)}
    db.write_text(json.dumps(stamps, indent=2), encoding="utf-8")
    argv = [sys.executable, str(Path(__file__).resolve()), "bootstrap"]
    # CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP: the job gets one hidden console
    # that every tool it starts shares. DETACHED_PROCESS had no console at all, so
    # each bash/cmake/ninja/cc1plus child opened (and flashed) its own window.
    flags = {"creationflags": 0x08000000 | 0x00000200} if IS_WINDOWS else {"start_new_session": True}
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
    fingerprint = source_fingerprint()  # taken before building: edits made meanwhile force the next run
    setup(REPO, build_dir)
    rc = build_binary(REPO, build_dir, args.jobs)
    if rc == 0 and fingerprint:
        db = state_dir() / "bootstrap.json"
        stamps = read_bootstrap_db(db)
        stamps.setdefault(str(REPO), {})["built"] = fingerprint
        db.write_text(json.dumps(stamps, indent=2), encoding="utf-8")
    sys.exit(rc)


ROADMAP_TOOL = Path(__file__).resolve().parent / "roadmap.py"
AI_EVAL_TOOL = Path(__file__).resolve().parent / "ai_eval.py"


def cmd_roadmap(args):
    """Generate the dependency view over the issues (docs/roadmap/roadmap.html)."""
    argv = [sys.executable, str(ROADMAP_TOOL), "generate"] + args.args
    if args.offline:
        argv.append("--offline")
    log(" ".join(shlex.quote(a) for a in argv))
    sys.exit(subprocess.run(argv, cwd=str(REPO)).returncode)


def cmd_ai_eval(args):
    """Run the AI evaluation matrix and report the battle metrics."""
    argv = [sys.executable, str(AI_EVAL_TOOL)] + args.args
    log(" ".join(shlex.quote(a) for a in argv))
    sys.exit(subprocess.run(argv, cwd=str(REPO)).returncode)


def dep_numbers(raw: str):
    """`#12, 34 #56` -> [12, 34, 56]; empty input -> [] (so `--clear` works)."""
    return sorted({int(n) for n in re.findall(r"#?(\d+)", raw or "")})


def cmd_dep(args):
    """Set an issue's native dependency relations (`gh issue edit` under the hood).

    Native relations are the source of truth for the graph; the prose lines stay
    in the body as human-readable annotation. Idempotent: reading the issue
    first means a re-run is a no-op, and only the difference is written.
    """
    if not shutil.which("gh"):
        die("gh not found on PATH")
    repo = f"{args.owner}/{args.repo}" if args.owner and args.repo else None
    base = ["gh", "issue", "view", "-R", repo, str(args.issue),
            "--json", "blockedBy,blocking,state,title"]
    view = subprocess.run(base, capture_output=True, text=True)
    try:
        current = json.loads(view.stdout)
    except ValueError:
        die(f"could not read issue {args.issue}: {(view.stderr or view.stdout).strip()}")

    # `--clear` wins over the lists: it drops every relation on the issue.
    blocked_by = [] if args.clear else dep_numbers(args.blocked_by)
    blocking = [] if args.clear else dep_numbers(args.blocking)
    have_blocked = sorted(n["number"] for n in current.get("blockedBy", {}).get("nodes", []))
    have_blocking = sorted(n["number"] for n in current.get("blocking", {}).get("nodes", []))
    # Adding is implicit; removing needs `--prune` (or comes with `--clear`), so
    # naming a subset never silently deletes the relations left out of it.
    remove = args.prune or args.clear

    def diff(want: list[int], have: list[int], add_flag: str, drop_flag: str) -> list[str]:
        flags = []
        add = [n for n in want if n not in have]
        drop = [n for n in have if n not in want]
        if add:
            flags += [add_flag, ",".join(str(n) for n in add)]
        if drop and remove:
            flags += [drop_flag, ",".join(str(n) for n in drop)]
        return flags

    flags = diff(blocked_by, have_blocked, "--add-blocked-by", "--remove-blocked-by")
    flags += diff(blocking, have_blocking, "--add-blocking", "--remove-blocking")

    log(f"#{args.issue} {current.get('title', '')} ({current.get('state', '').lower()})")
    log(f"  blocked by: {have_blocked or '-'} -> {blocked_by or '-'}")
    log(f"  blocking:   {have_blocking or '-'} -> {blocking or '-'}")
    if not flags:
        log("  already up to date; nothing to do")
        return
    edit = ["gh", "issue", "edit", "-R", repo, str(args.issue)]
    log("  " + " ".join(edit + flags))
    rc = subprocess.run(edit + flags).returncode
    if rc == 0:
        log("  updated")
    sys.exit(rc)


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
    print(f"{'cache:':<12}{SCCACHE_BASEDIRS}={REPO}"
          f" (+ {len(worktree_roots()) - 1} other checkout(s), one shared cache)")


# --- worktrees and PR screenshots --------------------------------------------

def tool_module(name: str):
    """One of the helper scripts next to dev.py, importable from any cwd."""
    tools = Path(__file__).resolve().parent
    if str(tools) not in sys.path:
        sys.path.insert(0, str(tools))
    try:
        return __import__(name)
    except OSError as exc:  # a broken tools/ tree is our fault, not the caller's
        die(f"could not load tools/{name}.py: {exc}")


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
        if arg == "--changed" and i + 1 < len(tail) and not tail[i + 1].startswith("-"):
            # `--changed <ref>`: argparse would take the next token as the
            # optional value, but split_tail runs first - claim it here or it
            # leaks into the ctest command line.
            dev += tail[i:i + 2]
            i += 2
            continue
        if (arg in DEV_FLAGS or arg.startswith("--jobs=") or re.fullmatch(r"-j\d+", arg)
                or arg == "--changed" or arg.startswith("--changed=")):
            dev.append(arg)
        else:
            child.append(arg)
        i += 1
    return dev, child


# Commands whose flags belong to a script of their own, dispatched before the
# parser is built so that `--help` reaches them instead of a dev.py subparser.
TOOL_COMMANDS = {
    "worktrees": "list this repo's worktrees (size, age, merged/PR state); --prune collects the dead ones",
    "shots": "publish a PR's screenshots to the orphan pr-screenshots branch and print the links",
}


def dispatch_tool_command(name: str, rest: list[str]):
    """Run one of TOOL_COMMANDS, or None if this is not one."""
    if name == "worktrees":
        return tool_module("worktrees").main(rest, repo=REPO, here=REPO,
                                             state_dir=state_dir(), log=log)
    if name == "shots":
        work = state_dir() / "pr-screenshots"
        # One publisher at a time machine-wide: two agents racing on the same
        # branch and worktree is exactly the collision this command exists to stop.
        with file_lock(state_dir() / "pr-screenshots.lock", "pr-screenshots publish"):
            return tool_module("shots").main(rest, repo=REPO, work=work, log=log)
    return None


def main():
    argv = sys.argv[1:]
    if argv and argv[0] in TOOL_COMMANDS:
        sys.exit(dispatch_tool_command(argv[0], argv[1:]))

    parser = argparse.ArgumentParser(
        prog="tools/dev.py",
        description="one entry point for building, testing and running ja2-pecel-bakwan (macOS and Windows)",
        epilog="other commands: " + "; ".join(f"{name} - {text}" for name, text in TOOL_COMMANDS.items())
               + "  (their own --help has the flags)",
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
    p.add_argument("--changed", nargs="?", const="master", default=None, metavar="REF",
                   help="run only the tests a diff against REF (default: master) can affect, plus smoke")
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

    p = sub.add_parser("roadmap", help="generate docs/roadmap/roadmap.html: the dependency view over the issues")
    p.add_argument("--offline", action="store_true", help="regenerate from the committed snapshot only")
    p.add_argument("args", nargs="*", help="extra flags for tools/roadmap.py")
    p.set_defaults(func=cmd_roadmap)

    p = sub.add_parser("ai-eval", help="run the AI evaluation matrix (tools/ai_eval.py)")
    p.set_defaults(func=cmd_ai_eval)

    p = sub.add_parser("dep", help="set an issue's native dependency relations (the roadmap's edges)")
    p.add_argument("issue", type=int, help="issue number")
    p.add_argument("--blocked-by", default=None,
                   help="issues this one waits for, e.g. '#96,#260'; replaces the list")
    p.add_argument("--blocking", default=None,
                   help="issues that wait for this one, e.g. '#97,#98'; replaces the list")
    p.add_argument("--clear", action="store_true", help="drop every relation on the issue")
    p.add_argument("--prune", action="store_true",
                   help="also remove relations missing from the lists given above")
    p.add_argument("--owner", default="adhikasp", help="repository owner")
    p.add_argument("--repo", default="ja2-pecel-bakwan", help="repository name")
    p.set_defaults(func=cmd_dep)

    # `worktrees` and `shots` were dispatched above, before this parser existed:
    # a subparser here would eat their `--help`. They are in the epilog instead.
    argv = sys.argv[1:]
    if argv and argv[0] in {"test", "e2e", "run", "ai-eval"}:
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
