# Agent guidelines

This repo is worked on from both macOS and Windows. Check which machine you're on before picking a command block below.

## macOS

### Daily build
```bash
source "$HOME/.cargo/env"
cmake --build build -- -j$(sysctl -n hw.logicalcpu)
```
Incremental — only rebuilds changed files. No need to re-run `cmake ..` unless `CMakeLists.txt` changed.

### Test
```bash
./build/ja2 -unittests
```

### Run
```bash
./build/ja2 -res 1280x720
```
Game data is at `~/Workspace/ja2-gamedir/app`, configured in `~/.ja2/ja2.json`. No flags needed for data dir.

## Windows (MSYS2 MinGW64)

Toolchain lives in MSYS2 at `C:\msys64` (packages: `mingw-w64-x86_64-toolchain`, `-rust`, `-cmake`, `-sdl3`, `-fltk`, plus `base-devel` for the `make` the "MSYS Makefiles" generator needs). Build directory is **`_bin`** here, not `build`. Every command needs the MinGW64 environment, so run through a login shell with `MSYSTEM=MINGW64` set — plain PowerShell/cmd won't have `gcc`/`cmake`/`cargo` on PATH.

### Daily build
```bash
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && make -j\$(nproc)"
```
Reconfigure only if `CMakeLists.txt` changed:
```bash
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && cmake .. -G 'MSYS Makefiles' -DCPACK_GENERATOR=ZIP"
```

### Test
```bash
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && ./ja2.exe -unittests"
```

### Run
```bash
MSYSTEM=MINGW64 "/c/msys64/usr/bin/bash.exe" -lc "cd '/c/Workspace/ja2-stracciatella/_bin' && ./ja2.exe -res 1280x720"
```
Game data comes from the Steam install: `C:\Program Files (x86)\Steam\steamapps\common\Jagged Alliance 2 Gold`, configured via `game_dir` in `%APPDATA%\JA2\ja2.json`.
**Gotcha:** `game_dir` must point at the install root (the folder containing `Data\`), not at the `Data` folder itself — pointing it at `Data` directly makes the VFS look for a nonexistent `Data\data` and fail with "Error initializing VFS ... os error 3".
Log file: `C:\msys64\tmp\ja2.log` (MSYS bash's own `/tmp`, not Windows `%TEMP%`).

## Driving the game (headless automation + E2E tests)
The game runs headless on a virtual clock and can be driven from the shell or by Lua scripts. Full guide: [docs/automation.md](docs/automation.md).
```bash
python tools/ja2ctl.py start                  # headless session at the main menu (-s NAME for more sessions)
python tools/ja2ctl.py ui                     # clickable elements with labels
python tools/ja2ctl.py text                   # visible text with positions
python tools/ja2ctl.py click "New Game"       # click by label, or: click X Y
python tools/ja2ctl.py shot                   # screenshot, prints the PNG path (Read it to look)
python tools/ja2ctl.py state                  # screen, time, money, mercs
python tools/ja2ctl.py eval 'return require("lib.campaign").startWithMerc("Barry").sector'
python tools/ja2ctl.py stop
python tools/ja2ctl.py run tests/e2e/<name>.lua --isolated   # one-shot script run
ctest -L e2e -j8 --output-on-failure          # all e2e tests, from the build dir (_bin on Windows)
```
Tests and their shared helpers (`lib/campaign.lua`) live in `tests/e2e/`; see its README.
Image-only widgets need `SetName(...)` in C++ to be clickable by label; new animations/loading states belong in `NothingInFlight()` in `src/game/Automation/AutomationSession.cc`.

## Codebase shape

| Path | What lives there |
|---|---|
| `src/game/` | Core game logic (combat, AI, UI screens) |
| `src/sgp/` | Platform layer — input, rendering, sound, file I/O |
| `src/externalized/` | Moddable data loaded from JSON at runtime |
| `src/launcher/` | FLTK-based launcher GUI |
| `dependencies/` | Vendored/downloaded libs (sol2, gtest, miniaudio, fltk) |
| `build/externalized/` | JSON data files copied at build time — edit originals in `src/externalized/` |

- Language: C++20 with Lua scripting via sol2.
- The Rust component (`dependencies/lib-stracciatella`) is a support library — most game code is C++.
- `.slf` files are the original JA2 archive format; the VFS layer transparently reads them.

## Where to look for things

- **Game screens** (main menu, tactical, map): `src/game/` — files named after the screen, e.g. `MapScreen.cc`, `TacticalView.cc`.
- **Save/load**: `src/game/SaveLoadGame.cc`.
- **Item/merc data**: JSON in `src/externalized/` — no recompile needed to tweak values.
- **Lua hooks**: `src/game/scripting/` — moddable behavior without touching C++.

## Dev mindset

- **Prefer `src/externalized/` JSON edits over C++ changes** when tweaking game data — no recompile, hot-reloadable.
- **The VFS is layered** — files in `build/externalized/` override `.slf` archives. Use this for rapid iteration on assets.
- **Unit tests are fast** — run `-unittests` before and after changes to catch regressions early.
- **Log output is your debugger** — the game logs to `/var/folders/.../ja2.log`; tail it while the game runs.
- **Don't touch `dependencies/`** — these are managed by cmake; changes get overwritten on reconfigure.

## GitHub: remotes, projects, issues and milestones

### Never touch upstream

- `origin` = **adhikasp/ja2-stracciatella** — our fork, the only copy we write to.
- `upstream` = **ja2-stracciatella/ja2-stracciatella** — read-only.

Never push to it, never open a PR or an issue there, never comment there, never add it to a project, never point a `gh` command at it. Repository commands carry `-R adhikasp/ja2-stracciatella`; project commands carry `--owner adhikasp`. Everything merges into this fork's `master`, which is protected — go through a PR, even for a docs-only change.

### What goes where

| Layer | Answers | Holds |
|---|---|---|
| Project board | What are we doing, in what order? | every planned slice of work, across all tracks |
| Issue | What is broken or undecided? | bugs and decisions that need a conversation |
| Milestone | What ships together, by when? | a bundle of merged PRs plus a target date |
| `docs/plan/`, `docs/ui/` | Why and how? | the plan, the M1 parity contract, the decisions |

Nothing is tracked twice. The board is the single source of order; an issue is only a discussion thread hanging off a board item; a milestone only groups PRs that must land together.

### The board

One public board: **JA2 Modernization Roadmap** — https://github.com/users/adhikasp/projects/1, linked to this repository. A new track is a new value in the `Track` field, never a second board.

- **`Track`** — `Modernize graphics` (leftover work from `docs/plan/native-modern-game.md`) · `Modernize AI` · `E2E tactical battles` · `E2E campaign` · `Port 1.13 features` · `Repo & CI`.
- **`Kind`** — `Goal` (one objective statement per track) · `Task` (an actionable slice; its body is the definition of done and cites the plan, spec or PR) · `Placeholder` (a track we have claimed but not planned — its first task is always "write the plan doc", and nothing else under that track starts before that doc exists).
- **`Status`** — `Backlog` → `Todo` → `In Progress` → `Done`, plus `Blocked` (the body says why).

Routine: create the item → move it to `In Progress` when you start → open one PR per item, titled with the phase or slice, body linking the board item → `Done` when the PR merges.

### Issues

Bugs and decisions only. Features, phases and planned work are board items, never issues. Titles start with `bug: ` or `decision: `. Labels: exactly one kind (`bug`, `question`, `decision`) and at most one area (`tactical`, `mapscreen`, `laptop`, `nativeui`, `world-renderer`, `build`, `automation`). When an issue belongs to planned work, add it to the board (`gh project item-add`) so it shows up in the order of things.

### Milestones

One per shippable increment — never per phase, never per PR:

- `Native modern: front end + map + tactical` — Phases 0–5 and 8 (delivered)
- `Native modern: laptop` — Phase 6
- `Native modern: remaining screens` — Phase 7
- `Native modern: HD art + legacy removal` — Phases 9 and 10
- `AI, e2e and 1.13` — inactive until those `Placeholder` tracks get a plan doc

Every PR carries the milestone of the board item it closes; a milestone is closed when its last PR merges. Housekeeping under `Repo & CI` is not a shippable increment and carries no milestone.

Plans stay in the repo: the board says *what* and *in what order*, `docs/plan/` keeps *why* and *how*, and the handover doc is refreshed when a phase lands.

## Every PR needs screenshot proof

A PR that changes anything the player can see (rendering, layout, UI, screens, video settings) must show screenshots of the result in its description. Passing tests is not enough. Golden images only prove a screen didn't *change*, not that it looks *right*.

1. **Take them from the real game.** Drive the build with `python tools/ja2ctl.py` (or a `--show` window) and use `ja2ctl shot`.
2. **Look at every one before opening the PR.** Open each PNG and check it. If a screen looks wrong, fix it; don't explain it away.
3. **Cover the affected screens at 640x480 and at least one widescreen size.** For example 1920x1080, plus 2560x1080 or 3440x1440 when the layout depends on width. When a PR adds or regenerates golden images, include the widescreen goldens it changes.
4. **Host them on the `pr-screenshots` branch.** It is an orphan branch that only holds images, so they never enter `master`'s history:
   ```bash
   git fetch origin pr-screenshots || true
   git worktree add ../pr-shots pr-screenshots 2>/dev/null || (git worktree add --detach ../pr-shots && cd ../pr-shots && git checkout --orphan pr-screenshots && git rm -rf . -q)
   mkdir -p ../pr-shots/<branch-name> && cp <shots>.png ../pr-shots/<branch-name>/
   cd ../pr-shots && git add . && git commit -m "Screenshots for <branch-name>" && git push origin pr-screenshots
   ```
   Embed them in the PR body with
   `![map 1920x1080](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/<branch-name>/map_1920x1080.png)`.
   Give each one a short caption saying what to look at.
5. **Reviewers open the images.** Don't merge a UI PR without doing so. If an image is missing or shows a problem, send the PR back.

A PR with no visible change (build, tests, tooling) says "No visual change" instead.
