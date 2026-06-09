# ja2-stracciatella — Claude context

## Daily build
```bash
source "$HOME/.cargo/env"
cmake --build build -- -j$(sysctl -n hw.logicalcpu)
```
Incremental — only rebuilds changed files. No need to re-run `cmake ..` unless `CMakeLists.txt` changed.

## Test
```bash
./build/ja2 -unittests
```

## E2E screen tests
In-process Playwright-style harness: drives the real rendered game with synthetic SDL input and asserts on pixels read from `ScreenBuffer`. Runs hidden/backgrounded so it never steals focus.
```bash
ctest -R e2e --test-dir build                 # run all e2e scripts
./build/ja2 -uitest tests/e2e/<name>.txt      # run one script
```
Scripts live in `tests/e2e/*.txt` (`load_save_to_mapscreen.txt` is the calibrated canonical one). For the script vocabulary and reusable calibrated fragments, see [tests/e2e/COOKBOOK.md](tests/e2e/COOKBOOK.md).

## Run
```bash
./build/ja2 -res 1280x720
```
Game data is at `~/Workspace/ja2-gamedir/app`, configured in `~/.ja2/ja2.json`. No flags needed for data dir.

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
