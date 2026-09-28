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

## E2E screen tests
In-process Playwright-style harness: drives the real rendered game with synthetic SDL input and asserts on pixels read from `ScreenBuffer`. Runs hidden/backgrounded so it never steals focus.
```bash
ctest -R e2e --test-dir build                 # run all e2e scripts (adjust dir to _bin on Windows)
./build/ja2 -uitest tests/e2e/<name>.txt      # run one script
```
Scripts live in `tests/e2e/*.txt` (`load_save_to_mapscreen.txt` is the calibrated canonical one). For the script vocabulary and reusable calibrated fragments, see [tests/e2e/COOKBOOK.md](tests/e2e/COOKBOOK.md).

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
