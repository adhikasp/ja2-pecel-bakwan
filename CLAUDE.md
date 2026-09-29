# ja2-stracciatella — Claude context

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
