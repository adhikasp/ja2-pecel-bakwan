# Agent guidelines

This repo is worked on from both macOS and Windows. **`python tools/dev.py <cmd>` is the one entry point on both** (macOS: `python3` if there is no `python`); on Windows it shells into MSYS2 MinGW64 itself. The raw commands it wraps are in [COMPILATION.md](COMPILATION.md#the-raw-commands-behind-toolsdevpy).

## Build, test, run

```bash
python tools/dev.py setup     # once per worktree (idempotent): build dir, uv sync, game_dir check
python tools/dev.py build     # incremental build
python tools/dev.py test      # C++ unit tests
python tools/dev.py e2e [..]  # e2e tests: ctest -L e2e, or one script (tests/e2e/foo.lua --isolated)
python tools/dev.py run [..]  # play the game (default -res 1280x720)
python tools/dev.py status    # what dev.py sees: paths, tools, build state
python tools/dev.py roadmap   # regenerate docs/roadmap/roadmap.html from the issues
python tools/dev.py dep <n> --blocked-by "#1,#2"   # set an issue's dependency relations
```

`roadmap` and `dep` are host-side and need no build: `roadmap` reads the GitHub issues through `gh` and writes the dependency view (see [The roadmap page](#the-roadmap-page)), `dep` sets an issue's native relations.

- The build directory is `build` on macOS and `_bin` on Windows; the wrapper creates, configures and updates it (Ninja + sccache + lld when the machine has them, see [Faster builds](COMPILATION.md#faster-builds)).
- **The compiler cache is shared between worktrees.** `tools/dev.py` sets `SCCACHE_BASEDIR` to the worktree root, so sccache rewrites those paths to be relative before hashing and two checkouts at different absolute paths get the same key: build `master` (or any branch) once and a fresh worktree of the same code reuses those objects, minutes instead of a full recompile. Worktrees build side by side — there is no shared build path to queue behind. Builds that bypass `tools/dev.py` do not set it and share nothing.
- Builds and e2e runs also take a machine-wide job semaphore, so several agents on one machine share the CPU instead of starving each other. `JA2_JOBS` or `--jobs` overrides the computed count.
- Deep worktrees (agent worktrees live in deep directories) stay well under Windows' MAX_PATH on their own: the deepest path this tree produces is ~212 characters, in cargo's `target/`, and Windows 10+ long-path support is on by default.
- `setup` also checks `game_dir` and warns about the `Data` trap below. Everything is idempotent: re-run `setup` after switching branches or moving a worktree.

### Worktree bootstrap runs itself

`python tools/dev.py bootstrap --auto` fires at session start — the OpenCode plugin in `.opencode/plugins/dev-bootstrap/` and the Claude Code `SessionStart` hook in `.claude/settings.json` both run it. It configures the worktree and builds it **in the background** (log: `~/.ja2-dev/logs/bootstrap-*.log`, `%LOCALAPPDATA%\ja2-dev\logs\` on Windows), and it dedupes, so several sessions starting at once are fine. A fresh worktree is buildable without asking; `python tools/dev.py status` shows where the build is.

## Platform notes

### macOS
Game data is at `~/Workspace/ja2-gamedir/app`, configured in `~/.ja2/ja2.json`. No flags needed for data dir.

### Windows (MSYS2 MinGW64)
Toolchain lives in MSYS2 at `C:\msys64` (packages: `mingw-w64-x86_64-toolchain`, `-rust`, `-cmake`, `-sdl3`, `-fltk`, plus `base-devel`. For the fast loop also install `mingw-w64-x86_64-sccache`, `-lld` and `-ninja` — cmake picks all three up by itself, see [Faster builds](COMPILATION.md#faster-builds)). `tools/dev.py` runs every build command inside that environment for you; plain PowerShell/cmd doesn't have `gcc`/`cmake`/`cargo` on PATH.

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
python tools/dev.py e2e                                       # all e2e tests (ctest -L e2e)
```
Tests and their shared helpers (`lib/campaign.lua`) live in `tests/e2e/`; see its README. `python tools/dev.py setup` runs `uv sync` for you — it creates `.venv/` with Pillow and numpy, which `ctest` picks up for the resolution/golden-image tests after a fresh cmake configure.
Image-only widgets need `SetName(...)` in C++ to be clickable by label; new animations/loading states belong in `NothingInFlight()` in `src/game/Automation/AutomationSession.cc`.

### Golden / resolution suite

`ctest -L resolution` is the comparator suite, and it defaults to **1080p only**: every tour at 1920x1080, checked for layout and compared with `tests/e2e/golden/1920x1080/`.

For a UI change, this default is enough — you do not need the full matrix in every resolution unless the change specifically alters how the game renders (for example adding a new menu, or updating a texture):

```bash
ctest -L resolution --output-on-failure       # the default: 1080p only
ctest -R resolution_ --output-on-failure      # full matrix: 640x480, 1280x720, 1920x1080, 2560x1080, 3440x1440
```

Ordinary UI work — labels, wiring, click handling, a screen's own logic — is not a rendering change and does not need the full sweep. Regenerate goldens with `JA2_UPDATE_GOLDEN=1` (or `check_resolution.py --update`); see `tests/e2e/golden/README.md`.

## Codebase shape

| Path | What lives there |
|---|---|
| `src/game/` | Core game logic (combat, AI, UI screens) |
| `src/sgp/` | Platform layer — input, rendering, sound, file I/O |
| `src/externalized/` | C++ loaders (`*Model` classes, `DefaultContentManager`) for the original game's JSON data |
| `assets/externalized/` | The original game's JSON data — frozen, see *Choosing a layer* |
| `rust/` | Frozen infrastructure behind a C API: VFS, `.slf`/STCI formats, `ja2.json` config, JSON schemas, logger |
| `tests/e2e/` | Lua scripts that drive the headless game, and their helpers (`lib/`) |
| `src/launcher/` | *(removed)* — the launcher's features are the native setup screen (`src/game/NativeUI/FrontSetup.cc`) |
| `dependencies/` | Vendored/downloaded libs (sol2, gtest, miniaudio) |

- Language: C++20 for the game. Lua (via sol2) drives and tests it; it holds no game rules.
- `.slf` files are the original JA2 archive format; the VFS layer transparently reads them.

## Choosing a layer

We optimize for one thing: **a fast, agent-drivable feedback loop.** Modding and runtime tweakability are not goals.

- **C++ is the game.** Every rule, mechanic, behavior and piece of new content goes here. A type error found by the compiler is cheaper than a runtime error found by driving the game to it, and one language keeps the whole call graph searchable.
- **Lua is the driving and test language only** — `tests/e2e/`, `lib/campaign.lua`, `ja2ctl eval`. Never put game logic in Lua.
- **Rust is frozen.** Don't add features to `rust/`; don't port it either. Touch it only when a change forces you to.
- **JSON is frozen.** No new files in `assets/externalized/`. New data is compiled C++ tables with unit-tested invariants (like `src/game/Content/PeopleContent.cc`). When a system is revamped, move its JSON into C++ in the same piece of work.

Every new system has the same shape:

1. A core with no globals, covered by gtest.
2. A thin adapter onto the legacy globals.
3. An `Observable` at each decision point.
4. A small Lua surface so `ja2ctl state`/`eval` can read its internals as data and set up its scenarios (extend `BattleScenario`/`CampaignScenario`) — asserting a result should never need a click path or a screenshot.

## Where to look for things

- **Game screens** (main menu, tactical, map): `src/game/` — files named after the screen, e.g. `MapScreen.cc`, `TacticalView.cc`.
- **Save/load**: `src/game/SaveLoadGame.cc`.
- **Item/merc data**: original JSON in `assets/externalized/`, loaded by `src/externalized/`; compiled content in `src/game/Content/`.
- **Automation and the Lua driving API**: `src/game/Automation/` (`AutomationLua.cc`, scenarios); NativeUI view models for tests in `src/game/NativeUI/ViewModelLua.cc`.

## Dev mindset

- **Native-first, no backward compatibility.** New gameplay and behavior are native C++ and the default. Do not add externalized rules, legacy fallbacks or compatibility shims; old saves, old behavior and old APIs are not a constraint.
- **Keep the loop short.** Iterate on the gtest core first; drive the headless game only to prove integration. If a cycle (edit → build → test) gets slow, fix the build or the test surface rather than moving logic out of C++.
- **The VFS is layered** — files in the build's `externalized/` override `.slf` archives. Use this for asset iteration.
- **Unit tests are fast** — `python tools/dev.py test` before and after changes to catch regressions early.
- **Log output is your debugger** — see *Platform notes* for the log path; tail it while the game runs.
- **Don't touch `dependencies/`** — these are managed by cmake; changes get overwritten on reconfigure.

## Game design principles

This fork is not a preservation project. It is a modern reimplementation of Jagged Alliance 2 with a richer tactical and strategic game. These principles are the tie-breaker for design decisions: when a plan or a PR conflicts with one, the plan changes.

- **The revamp is the game.** New mechanics ship on by default. There is no vanilla mode, no byte-compatible save format and no obligation to preserve old behavior. We own the rules, the balance and the save format.
- **Native-first.** Gameplay systems live in C++ in the engine; we do not externalize rules or data to runtime JSON or Lua. Balance values are compiled constants; `assets/externalized/` only holds the original game's data until its system is revamped.
- **Testable by construction.** Every system has a C++ unit-test surface and a deterministic `ja2ctl`/e2e driving surface. If a mechanic cannot be asserted headless, it is not done.
- **Curate.** Take the design idea and ship a small, balanced set, not a catalog. Content is compiled, with unit-tested invariants.
- **A rich tactical layer.** Equipment is a system (attachments, ammo, condition), combat is a simulation (NCTH, suppression, morale, stances, detection, vision/light), and the soldier is a character (traits, medical, encumbrance, covert ops, melee, breaching).
- **A rich strategic layer.** The world is alive. A deed ledger tracks whether the player is the savior or the next dictator; towns, factions and NPCs react and remember; militia, facilities, the economy and the strategic sector inventory give the map teeth.
- **The Queen has a mind.** A native emotional drive state and a native policy layer drive her strategic actions. An optional LLM layer provides her voice and turns the player's free text into a bounded intent set, but the LLM never writes game state; headless runs use a deterministic stub.
- **Reimplement ideas, never port code.** Ideas from other JA2 projects are reimplemented natively; their code, UI and runtime data files are not imported.
- **Agent-drivable, not moddable.** The game is built to be developed and played headless by an agent through `ja2ctl` and Lua. There is no mod surface to preserve: the Lua mod-scripting API (`src/externalized/scripting/`) and `assets/mods/` are slated for removal, and no new code may depend on them.

## GitHub: remotes, issues, projects and milestones

### Never touch upstream

- `origin` = **adhikasp/ja2-pecel-bakwan** — our fork, the only copy we write to.
- `upstream` = **ja2-stracciatella/ja2-stracciatella** — read-only.

Never push to it, never open a PR or an issue there, never comment there, never add it to a project, never point a `gh` command at it. Repository commands carry `-R adhikasp/ja2-pecel-bakwan`; project commands carry `--owner adhikasp`. Everything merges into this fork's `master`, which is protected — go through a PR, even for a docs-only change.

### What goes where

| Layer | Answers | Holds |
|---|---|---|
| Issue | What is the work? What is broken or undecided? | one issue per slice of work — tasks, bugs and decisions alike |
| Project board | In what order do those issues happen, across all tracks? | the same issues, with `Track` / `Kind` / `Status` set on them |
| Milestone | What ships together, by when? | the issues and PRs of one shippable increment |
| `docs/plan/`, `docs/ui/` | Why and how? | the plan, the M1 parity contract, the decisions |

**The issue is the unit of work** and it lives in the repository, so the backlog is simply the issue list: https://github.com/adhikasp/ja2-pecel-bakwan/issues. The board holds those issues — never its own copies (no draft items) — so nothing is ever tracked twice.

### Issues

- **Open one for every slice of work**, planned or discovered. Planned work also goes onto the board (below).
- **Ad-hoc work goes straight to a PR.** An issue is the home of planned work. A spontaneous fix or one-off correction — a typo, a broken build, a small cleanup — does not need an issue first; open the PR directly. File the issue only if it turns out to be a slice of planned work.
- **Title** — descriptive; prefixed `bug: ` or `decision: ` for those two kinds. Tasks and goals carry no prefix; the label already says what it is.
- **Labels** — exactly one type: `task`, `bug`, `decision`, `question` or `goal`; plus at most one area: `tactical`, `mapscreen`, `laptop`, `nativeui`, `world-renderer`, `build`, `automation`.
- **Body** — the definition of done, and the plan, spec or PR it comes from.
- **Milestone** — the increment it ships in (below). A follow-up to an already delivered increment carries none.
- **Dependencies** — native GitHub issue relations, which render in the issue sidebar and are what the roadmap page draws: `python tools/dev.py dep <issue> --blocked-by "#96,#260" --blocking "#97"`. Keep the prose lines too (below).
- **Board** — `gh project item-add 1 --owner adhikasp --url <issue url>`, then set `Track`, `Kind` and `Status`.

Routine: create the issue → add it to the board → `In Progress` when you start → one PR that says `Closes #N`, carrying that issue's milestone and the screenshot proof below → `Done` when the PR merges.

### Dependencies between issues

Edges are GitHub's **native issue relations** — `blockedBy` / `blocking` — set with `python tools/dev.py dep <issue> --blocked-by "#96,#260" --blocking "#97"` (idempotent: it reads the issue first and writes only the difference; `--prune` also drops relations missing from the lists, `--clear` drops them all). They render natively in GitHub's own issue sidebar, so declaring one improves the GitHub page too.

The body keeps the prose, because one relation cannot carry the nuance: whether a blocker is a work item or a **gate** (a fixture, a proof, a decision), and why.

| Write this in the body | It means | Native relation |
|---|---|---|
| `**Depends on:** #96, #260` | I cannot start until those land | `blockedBy: 96, 260` |
| `**Gated by:** #263 (fixtures)` | a gate, not work | `blockedBy: 263` |
| `**Feeds:** #220, #108` | those wait for me | `blocking: 220, 108` |
| `**Unblocks:** #97, #98` | the same edge, other end | `blocking: 97, 98` |

`## Builds on / depends on` sections of `#N` lists count as `Depends on`. A bare `#N` anywhere else stays prose — "Absorbed from #144" is not a live blocker — and the page shows those as a faint *mentions* count so the gaps stay visible. If the page and a relation ever disagree, the relation wins and `python tools/roadmap.py check` says where.

### The roadmap page

The backlog's shape lives in GitHub, but GitHub cannot draw it: milestones group by increment, the board groups by track, and neither shows the interdependency. **The roadmap page** is the dependency view — a generated view of the issues, never a second copy (if it and GitHub disagree, GitHub is right). Published at https://adhikasp.github.io/ja2-pecel-bakwan/roadmap/; regenerate locally with

```bash
python tools/dev.py roadmap        # refresh from GitHub, write docs/roadmap/roadmap.html
python tools/dev.py roadmap --offline   # re-render from the snapshot of the last run
python tools/roadmap.py check      # prose/relation drift and dependency cycles
```

The page and its snapshot are **gitignored generated output** — not in the repo, not in a PR. Only `docs/roadmap/README.md` is. Output is deterministic: same input, byte-identical file.

Three linked views over the same graph: the **dependency DAG** (left to right by dependency depth, coloured by track), **milestone lanes** with the cross-milestone edges drawn over them, and the **critical path** with the issues whose delay costs the most. Filter by track / status / label / milestone, search, and a side panel with the issue's dependencies and body. The canvas owns the window — `f` and `d` fold the sidebars away, `1` `2` `3` switch view, `0` fits.

### The board

One public board: **JA2 Modernization Roadmap** — https://github.com/users/adhikasp/projects/1, linked to this repository. It is an ordering view over the issues above, not a second backlog. A new track is a new value in the `Track` field, never a second board.

- **`Track`** — `Modernize graphics` (leftover work from `docs/plan/native-modern-game.md`) · `Modernize AI` · `E2E tactical battles` · `E2E campaign` · `Port 1.13 features` · `Revamp gameplay` (design principles in `docs/plan/revamp-gameplay.md`) · `Repo & CI`.
- **`Kind`** — `Goal` (one objective statement per track) · `Task` (an actionable slice; its body is the definition of done and cites the plan, spec or PR) · `Placeholder` (a track we have claimed but not planned — its first issue is always "write the plan doc", and nothing else under that track starts before that doc exists).
- **`Status`** — `Backlog` → `Todo` → `In Progress` → `Done`, plus `Blocked` (the issue body says why).

### Milestones

One per shippable increment — never per phase, never per PR. The list of milestones lives on GitHub, not in this file: read https://github.com/adhikasp/ja2-pecel-bakwan/milestones (or `gh api repos/adhikasp/ja2-pecel-bakwan/milestones`) for the current one, and open a new milestone there when a new shippable increment appears. A milestone for a track that is still `Placeholder` says so in its description and stays inactive until that track's plan doc exists.

An issue and its PR carry the milestone of the increment they belong to; a milestone is closed when its last PR merges. Housekeeping under `Repo & CI` is not a shippable increment and carries no milestone.

Plans stay in the repo: the issues and the board say *what* and *in what order*, `docs/plan/` keeps *why* and *how*, and the handover doc is refreshed when a phase lands.

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
   `![map 1920x1080](https://raw.githubusercontent.com/adhikasp/ja2-pecel-bakwan/pr-screenshots/<branch-name>/map_1920x1080.png)`.
   Give each one a short caption saying what to look at.
5. **Reviewers open the images.** Don't merge a UI PR without doing so. If an image is missing or shows a problem, send the PR back.

A PR with no visible change (build, tests, tooling) says "No visual change" instead.
