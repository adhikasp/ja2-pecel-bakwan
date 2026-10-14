---
name: bagong-build-box
description: Use this skill to build, test or run ja2-pecel-bakwan on bagong, the primary remote build machine (Ryzen 5 5600, Windows 11 + MSYS2). Prefer it over a local build for clean builds, the unit tests and the e2e and resolution suites; build locally only when bagong is already held by another session. Also use it when asked to verify something on Windows/MSYS2, or to hand a long build or test sweep to a faster machine.
---

# bagong — the primary remote build machine

`bagong` is a Windows 11 desktop (Ryzen 5 5600, 6 cores / 12 threads, 32 GB RAM) with the
whole MSYS2 MinGW64 toolchain, a clone of this repo and the original game data. It is
reachable as `ssh bagong` (already in `~/.ssh/config` on the dev machines; key
`~/.ssh/id_ed25519`).

**Treat it as the default place to build and test.** It has the fastest CPU of the machines
that can build this tree: about 1.7× the laptop that usually drives the agent on a
from-scratch build, and 1.5–1.6× on the full test sweeps (see
[Measured timings](#measured-timings)). Build locally only when bagong is already busy, or
when you are inside the edit → build loop on a few files and the ssh hop costs more than the
seconds it saves.

## Is it free?

Every build and test on a machine registers in a machine-wide registry, and `dev.py status`
prints the count:

```sh
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\dev.py status"
# ...
# active:     0 build/test process(es) on this machine
```

`active: 0` → nothing else is building or testing, use the box. **Anything else → build
locally instead**, do not queue behind it. Two concurrent builds are safe (`dev.py`
serialises them), but this is a shared personal machine and the point of the rule is to keep
one session on it at a time. Taking it means taking the shared clone with it (see
[Getting a branch onto it](#getting-a-branch-onto-it)), so the check is also what keeps two
sessions from checking different branches out underneath each other.

## What is already installed

Do not re-provision. As of 2026-10-14 the box has:

| | |
|---|---|
| Repo | `C:\Workspace\ja2-pecel-bakwan` (clone of `adhikasp/ja2-pecel-bakwan`, on `master`) |
| Build dir | `_bin` (Ninja), inside whichever worktree you build |
| Game data | `C:\Workspace\ja2-gamedir\app` (contains `Data\`, 861 MB) |
| `game_dir` | set in `%APPDATA%\JA2\ja2.json` → `C:/Workspace/ja2-gamedir/app` |
| Compiler | MSYS2 MinGW64 GCC 16.2, cmake 4.4.4, ninja 1.13.2, lld 23.1.3 |
| Rust | cargo/rustc 1.99.0 (`mingw-w64-x86_64-rust`) |
| Cache | `sccache` 0.18.0, 50 GB, one server per machine |
| Python | 3.12 at `C:\Program Files\Python312` (on the default `PATH`), `uv` at `%USERPROFILE%\.local\bin\uv.exe` |
| MSYS2 | `C:\msys64`; `tools\dev.py` shells into it by itself, you do not have to |

`python` and `git` are on the `PATH` of a plain `ssh bagong` session, and `dev.py` finds `uv`
itself. `gcc`, `cmake`, `ninja` and `ctest` are **not** — they live in the MSYS2 environment
that `dev.py` enters for you. Raw `ctest` needs it added (see
[the suites](#everyday-commands)).

## Everyday commands

`ssh bagong "..."` runs in **cmd.exe**, so use Windows paths and `&&`:

```sh
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\dev.py setup"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\dev.py build"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\dev.py test"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\dev.py e2e"

# the resolution/golden suite is not behind a dev.py subcommand:
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan\_bin && set PATH=C:\msys64\mingw64\bin;%PATH% && ctest -L resolution --output-on-failure"
```

Nothing needs a display: `test`, `e2e` and everything else headless initialise SDL events
only and create no window (see [Driving the game](#driving-the-game)), so the same commands
work over ssh and out of CI. A build does eat all 12 threads for as long as it runs, so do not
start one and walk away from a box you share.

## Getting a branch onto it

The box builds **commits**, not working trees, and there is one shared clone at
`C:\Workspace\ja2-pecel-bakwan`. **Move that clone onto your branch and build there** — it is
the checkout whose `_bin` and compiler cache are already warm, so switching a branch usually
costs a handful of recompiles, where a fresh checkout costs the whole 8-minute build:

```sh
# on the machine you are editing on
git push -u origin <branch>

# on bagong — one line, so the whole switch is one round trip
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && git fetch origin && git checkout <branch> && git reset --hard origin/<branch>"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\dev.py build"
```

Leave the clone clean and back on `master` when you are done with it — you took the box, so
hand it back in the state you found it:

```sh
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && git checkout -f master && git reset --hard origin/master"
```

A **worktree** is the right tool only when you need two checkouts of the tree side by side
(your branch and `master`, say). It keeps the sessions from fighting over one checkout, but it
pays for its own `_bin` (~2.7 GB) and its own `uv sync`, and today it does **not** get
compiler-cache hits from the other checkouts — a fresh worktree is a full cold build, ~8 min
on this box. Until that is fixed (see the gotcha below), prefer the shared clone.

```sh
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && git fetch origin <branch>:<branch>"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && git worktree add C:\Workspace\ja2-box\<name> <branch>"
ssh bagong "cd /d C:\Workspace\ja2-box\<name> && python tools\dev.py setup"
# ...and when you are done
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && git worktree remove --force C:\Workspace\ja2-box\<name>"
```

To test something you have not committed yet, push the working tree as a scratch branch
rather than editing on the box:

```sh
git push origin "$(git stash create)":refs/heads/wip/<name>   # commits the working tree without touching it
```

Delete the scratch branch once it has been built.

## Measured timings

Same commit, same commands, 12 jobs, measured 2026-10-14 against the laptop that normally
drives the agent (Ryzen 5 5560U, 13 GB RAM — a 15–25 W mobile part against this 65 W desktop
one):

| Operation | bagong | laptop | bagong's edge |
|---|---|---|---|
| `setup` (cmake configure) | 35 s | 50 s | 1.4× |
| `build` from scratch, cold cache | **8m25s** | 14m32s | 1.7× |
| `build`, nothing to do | **2 s** | 4 s | 2.2× |
| `build` after touching one `.cc` | **3 s** | 6 s | 2.0× |
| `test` (601 unit tests) | 18 s | 20 s | 1.1× |
| `e2e` (59 tests) | **4m38s** | 7m23s | 1.6× |
| `ctest -L resolution` (36 tests, sequential) | **15m21s** | 23m21s | 1.5× |

The long suites are where it pays, and they track the CPU almost exactly: the game runs on a
virtual clock, so those tests spend their time computing, not waiting on I/O. Only the unit
tests are close to a wash — and they are also the one thing that is cheap enough to run
locally while the box is busy.

## Driving the game

`tools/ja2ctl.py` works over ssh exactly as it does locally — start a session, read the UI,
take a screenshot, run Lua:

```sh
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\ja2ctl.py -s box start"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\ja2ctl.py -s box state"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\ja2ctl.py -s box shot"
ssh bagong "cd /d C:\Workspace\ja2-pecel-bakwan && python tools\ja2ctl.py -s box stop"
```

(`-s <name>` is a global flag: it goes **before** the subcommand.)

The screenshot lands under `%USERPROFILE%\.ja2ctl\sessions\...` on the box and prints its
path; `scp` it back to look at it. **`-show` is not available** — a process started by sshd
has no interactive desktop on Windows, so keep to headless runs there. Play the game on that
box through its own console session if you need to watch it.

## Housekeeping and gotchas

- **`ssh bagong` is cmd.exe.** Local quoting is the usual Windows hazard; for anything
  multi-line, base64 the script and decode it on the box rather than piping it through the
  ssh command line, and never pipe binary through a PowerShell pipeline (`tar | ssh` corrupts
  the stream — use `scp -r`).
- **Cross-checkout cache hits are broken right now, and it is not this box's fault (#376).**
  `tools/dev.py` sets `SCCACHE_BASEDIR`, but sccache 0.16+ reads `SCCACHE_BASEDIRS` (plural,
  added with multi-path support) and silently ignores the singular one: the server reports
  `Base directories (none)` and every checkout of the tree hashes its own absolute paths, so a
  fresh worktree compiles from scratch. Setting the plural name makes the server report the
  roots correctly. This is why the shared clone above is the recommended path; until it is
  fixed, expect a cold build per new checkout.
- **`dev.py status`'s `active:` line is the whole busy check.** Check it before a suite, not
  after you have started one.
- **Check the cache before committing to a long run.** `sccache --show-stats` (on the box)
  prints hits, misses and the size; a cold build is ~790 targets and 8 minutes, and
  `SCCACHE_CACHE_SIZE` is 50 GB, so a surprising miss rate is worth a look rather than a
  patient wait.
- **`%APPDATA%\JA2\ja2.json` must have no BOM.** Writing it from PowerShell 5.1 with
  `Set-Content -Encoding utf8` adds one, `json.load` then fails, and `dev.py` reports the
  unhelpful "game_dir is not set". Write it with `[IO.File]::WriteAllText(..., UTF8Encoding($false))`.
- **First-time MSYS2 provisioning needs two passes.** `pacman -Syu` replaces
  `msys2-runtime` and the running shell dies with it; run it again afterwards. It is also
  worth pinning a mirror you can actually reach before installing
  `mingw-w64-x86_64-{cmake,ninja,lld,sccache,rust,toolchain}`.
- **Do not regenerate goldens on the box.** The goldens are portable machine-to-machine (the
  resolution suite passes 36/36 there), so a failure is a real failure: fix it, do not
  re-baseline. Regenerate on the machine whose change it is.
- **Clean up after yourself.** Remove a worktree you are finished with: each one holds a
  ~2.7 GB `_bin`, and it is someone's desktop C: drive.
- **The box is not dedicated build infrastructure.** It is a personal machine in another
  house that also serves game streaming and hosting, so do not leave multi-hour builds
  running unattended, and expect a build there to be a little slower if something else is
  playing on it.
