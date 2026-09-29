# E2E Screen Automation — Playwright-style Black-box Game Testing

> **Status: superseded** by [headless-automation.md](headless-automation.md) and the user guide
> [docs/automation.md](../automation.md). The `UITestDriver` described here has been replaced by the
> automation layer in `src/game/Automation/`; `-uitest script.txt` still runs old scripts by
> translating them to Lua. Kept for the reasoning about in-process input injection.

## Goal

Drive the **real rendered game** with synthetic mouse/keyboard input and verify state by
reading pixels off the screen. The interaction vocabulary is deliberately tiny and Playwright-like:

```
move   X Y            # move cursor to logical pixel (X, Y)
click  X Y            # left-click at (X, Y)
key    ESCAPE         # press a key
wait   1500           # sleep 1500 ms (let animations/loads settle)
assertpixel 320 240 #1f6f1f  8   # assert pixel is this color within tolerance 8
screenshot out.png    # dump current frame for the agent to look at
```

A coding agent (like Claude) writes one of these scripts, runs it, and gets an **exit code +
log** back — no human watching the window. This is the *coarse* first pass. The *fine-grained*
follow-up that drives game logic directly from Lua and asserts on game state lives in
[lua-scripttest-playwright.md](lua-scripttest-playwright.md). The two are complementary:

| | This plan (e2e-screen) | lua-scripttest |
|---|---|---|
| Drives | Real SDL input events | Internal C++ functions |
| Asserts on | Screen pixels | Game-state objects |
| Renders | Yes (real window or offscreen) | No (headless) |
| Catches | UI regressions, layout, rendering, click hit-boxes | Logic/combat/economy regressions |
| Brittleness | Higher (coordinates, art) | Lower |
| Good first target | "load save → tactical → move merc → assert" | combat math, hiring, time |

---

## Why in-process injection, not OS automation

The obvious approach is external: launch `ja2`, drive it with `cliclick`/`pyautogui`, capture
with macOS `screencapture`, read pixels from the PNG. It needs **zero code changes** and is worth
keeping as a Phase 0 smoke path (see below). But for anything reliable it fights three problems:

1. **HiDPI / Retina.** The game runs at a logical size (`SCREEN_WIDTH`×`SCREEN_HEIGHT`) via
   `SDL_RenderSetLogicalSize` ([Video.cc:133](../../src/sgp/Video.cc)). On a Retina display
   `screencapture` returns *physical* pixels (2×) plus window-chrome offset, so script coordinates
   never match game coordinates without a fragile calibration step.
2. **Focus & timing.** OS input goes to whatever window is frontmost; a notification stealing focus
   silently breaks the run. There's no clean "frame is now drawn" signal to sync against.
3. **Determinism.** Reading a saved PNG races the compositor; colors get gamma/profile-mangled.

The game already has exactly the two hooks we need to avoid all of this:

- **Input:** `MainLoop()` is a plain `SDL_PollEvent` dispatch loop
  ([SGP.cc:115](../../src/sgp/SGP.cc)). It already injects synthetic events — `requestGameExit()`
  does `SDL_PushEvent(SDL_QUIT)` ([SGP.cc:110](../../src/sgp/SGP.cc)). We can push synthetic
  `SDL_MOUSEMOTION` / `SDL_MOUSEBUTTONDOWN` / `SDL_KEYDOWN` events through the *same* dispatch that
  real input uses (`MouseMove`, `MouseButtonDown`, `KeyDown` in
  [Input.cc](../../src/sgp/Input.cc)). The game cannot tell the difference.
- **Pixels:** the final composited frame lives in the `ScreenBuffer` `SDL_Surface`
  ([Video.cc:57](../../src/sgp/Video.cc)) before it's uploaded to the GPU texture
  ([Video.cc:534](../../src/sgp/Video.cc)). Reading `ScreenBuffer->pixels` gives exact **logical**
  pixels, no Retina scaling, no compositor, no window chrome.

So coordinates in the script are game coordinates, pixel reads are exact, and the run is
deterministic and headless-capable. This is the recommended path. The trade-off: it requires
compiling a small driver into the game (Phases 1–4).

---

## Architecture

```
./build/ja2 -uitest tests/e2e/load_to_tactical.txt
        │
        │  main() detects flag → installs UITestDriver, runs game normally
        ▼
   MainLoop()  (unchanged dispatch)
        │
        │  each iteration, before SDL_PollEvent:
        ├─ UITestDriver::pump()
        │     ├─ if waiting on a `wait`/settle timer, return
        │     ├─ else pop next command from the script
        │     │     move/click/key  → SDL_PushEvent(synthetic event)
        │     │     wait            → arm timer
        │     │     assertpixel     → read ScreenBuffer, compare, log PASS/FAIL
        │     │     screenshot      → SDL_SaveBMP/PNG of ScreenBuffer
        │     └─ when script is exhausted → requestGameExit() with exit code
        │
        ▼
   process exits 0 (all asserts passed) / 1 (any failed) / 2 (driver/setup error)
```

The driver is *passive*: it injects an event then yields control back to the normal loop so the
game runs its own cycles, advances time, blits frames. It only acts again on the next loop
iteration. This keeps every game subsystem on its real code path.

---

## Implementation phases

### Phase 0 — External smoke path (NOT implemented — superseded)

> **Not built.** The in-process driver below was implemented directly, so this OS-automation
> fallback was never needed. Kept here for context only.

Prove the loop end-to-end before writing any C++. Pure shell/Python the agent can run now.

1. Launch fixed-size, fixed-position: `./build/ja2 -res 1280x720` (already supported). On macOS,
   move the window to a known origin with AppleScript so capture coordinates are stable.
2. Input: `cliclick` (`brew install cliclick`) — `cliclick c:640,360` to click, `kp:esc` for keys.
3. Capture a region: `screencapture -x -R<x>,<y>,<w>,<h> /tmp/frame.png`.
4. Read a pixel in Python (Pillow): `Image.open('/tmp/frame.png').getpixel((x,y))`.
5. Wrap as a throwaway script: launch → sleep → click → sleep → capture → assert pixel.

**Done when:** an agent can, with no game changes, click "Continue" on the main menu and assert a
non-black pixel appears where the tactical view should be. Document the Retina 2× caveat inline so
the next reader knows why coordinates are doubled.

> Phase 0 is the fallback the agent can reach for immediately. Everything below makes it robust.

---

### Phase 1 — `-uitest` flag + driver skeleton

**Goal:** `./build/ja2 -uitest <script>` boots the game normally and runs the script driver.

1. Parse `-uitest <path>` in `main()` before the Rust `EngineOptions` parser sees argv. The Rust
   CLI parser rejects unknown flags, so the implementation scans argv and **strips** `-uitest
   <path>` out (compacting the array) after recording the script path and setting the
   `g_uitest_mode` global. This runs before SDL init. See [SGP.cc](../../src/sgp/SGP.cc).
2. New files `src/sgp/UITestDriver.h` / `.cc`. `UITestDriver` owns: the parsed command list, a
   program counter, a `wait-until` timestamp, and a `failures` counter.
3. Parse the script once at startup into a `std::vector<Command>` (one struct per verb). Fail fast
   with exit code 2 on a malformed line, printing `line N: <reason>`.
4. Hook `UITestDriver::pump()` into the top of the `MainLoop()` body
   ([SGP.cc:119](../../src/sgp/SGP.cc)), guarded by `g_uitest_mode`. For Phase 1 it only handles
   `wait` and end-of-script (`requestGameExit()`), proving the lifecycle.

**Done when:** a script containing only `wait 2000` boots the game, idles 2 s, and exits cleanly
with code 0.

---

### Phase 2 — Input injection (move / click / key)

Translate commands into synthetic SDL events pushed through the existing dispatch.

```cpp
// move X Y
SDL_Event e{}; e.type = SDL_MOUSEMOTION;
e.motion.x = X; e.motion.y = Y;          // logical coords; mirror to gusMouse*Position
SDL_PushEvent(&e);

// click X Y  → motion, then button down, then (next pump) button up
e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT;
e.button.x = X; e.button.y = Y; SDL_PushEvent(&e);
// schedule the matching SDL_MOUSEBUTTONUP one pump later so the UI sees a real press/release gap
```

Key points:
- Coordinates are **logical** (0..`SCREEN_WIDTH`, 0..`SCREEN_HEIGHT`) — exactly what scripts use.
- A click must be **move → down → (gap) → up**; many JA2 buttons latch on the up edge and need the
  cursor already at the position. Insert a one-pump (or short `wait`) gap between down and up.
- Keys: map a small name table (`ESCAPE`, `ENTER`, `SPACE`, `F1`…, single chars) to `SDL_Keycode`
  and push `SDL_KEYDOWN`/`SDL_KEYUP`. For typed text (IMP name entry) push `SDL_TEXTINPUT`.
- Inject through `SDL_PushEvent`, **not** by calling `MouseButtonDown()` directly — going through
  the queue keeps ordering identical to real input and picks up any modifier state.
- The implementation also mirrors the cursor into `gusMouseXPos` / `gusMouseYPos` so the UI
  reflects the new position immediately, declaring them `extern` inline in `UITestDriver.cc`
  rather than adding an `Input.h` helper (so `Input.h` was left untouched).

**Done when:** a script can move to the main-menu "Continue/Load" button, click it, and the load
screen appears (verified by eye via a `screenshot` command, then by `assertpixel` in Phase 3).

---

### Phase 3 — Pixel assertion + screenshot

Read straight from `ScreenBuffer` ([Video.cc:57](../../src/sgp/Video.cc)) — the same surface the
renderer samples at [Video.cc:534](../../src/sgp/Video.cc).

```cpp
// assertpixel X Y #RRGGBB tol
SDL_LockSurface(ScreenBuffer);
Uint8* p = (Uint8*)ScreenBuffer->pixels + Y*ScreenBuffer->pitch + X*ScreenBuffer->format->BytesPerPixel;
Uint32 raw = /* read BytesPerPixel bytes */;
Uint8 r,g,b; SDL_GetRGB(raw, ScreenBuffer->format, &r,&g,&b);
SDL_UnlockSurface(ScreenBuffer);
bool ok = abs(r-er)<=tol && abs(g-eg)<=tol && abs(b-eb)<=tol;
```

- **Tolerance is mandatory.** JA2 art is dithered/shaded; never assert an exact RGB. Default
  tolerance 8, allow per-assert override.
- Expose `ScreenBuffer` to the driver via a tiny accessor in [Video.cc](../../src/sgp/Video.cc)
  (`SDL_Surface* GetScreenBufferForTest()`), guarded by `g_uitest_mode`, rather than making the
  static global public.
- `screenshot out`: the implementation writes **two** files — a `.bmp` via `SDL_SaveBMP` (always
  available) **and** a quality-85 `.jpg` via the vendored single-header `stb_image_write.h`. The
  JPEG is small and fast for an AI agent to eyeball when an assert fails — the single most useful
  debugging affordance here. Any extension on the path is stripped and both are emitted from the
  basename.
- Log format mirrors the lua plan for consistency:
  ```
  [uitest] [PASS] pixel(320,240) == #1f6f1f (got #1e6e1f, tol 8)
  [uitest] [FAIL] pixel(640,360) expected #ffffff, got #0a0a0a, tol 8
  [uitest] Script complete - 1 failure(s) of 6 total command(s)
  ```
  (The final summary counts total commands, not just assertions.)
- `pump()` sets the process exit code from the failure count when the script ends.

**Done when:** a deliberately wrong `assertpixel` exits 1 and prints the expected/got colors; a
correct one exits 0.

---

### Phase 4 — Settle / sync helpers (kill flakiness)

Raw `wait <ms>` is a blunt instrument; loads and fades vary. Add cheaper sync primitives:

- `waitstable X Y [timeout]` — poll the pixel at (X,Y) each pump; proceed once it stops changing
  for ~N frames or `timeout` elapses. Great for "wait until the screen finished fading in."
- `waitpixel X Y #RRGGBB [tol] [timeout]` — block until a pixel reaches a color (e.g. wait for the
  tactical HUD to draw) then continue; fail the run on timeout. This is the Playwright
  `expect(locator).toBeVisible()` analogue.
- Keep a global per-command default settle (e.g. 1 frame) so scripts read cleanly.

**Done when:** the load→tactical script uses `waitpixel` instead of a hard `wait` and passes
reliably across 10 consecutive runs.

---

### Phase 5 — Offscreen / windowless mode

As implemented, `-uitest` mode **always** creates the window hidden — there is no separate
`g_headless_mode` flag. When `g_uitest_mode` is set:

- `SDL_WINDOW_HIDDEN` is OR'd into the window flags in `InitializeVideoManager()`
  ([Video.cc](../../src/sgp/Video.cc)), and `SDL_HINT_MAC_BACKGROUND_APP` is set before `SDL_Init`
  ([SGP.cc](../../src/sgp/SGP.cc)). The original motivation was CI/no-display; in practice the
  bigger win is that **launching a test never steals keyboard/window focus** from the user.
- `ScreenBuffer` is a CPU `SDL_CreateRGBSurface` and is populated regardless of window visibility,
  so `assertpixel`/`screenshot` keep working with the window hidden — no renderer changes were
  needed and the planned software-renderer fallback was not required.
- To watch a run live, comment out the `SDL_WINDOW_HIDDEN` line in `InitializeVideoManager()`.

**Done:** `-uitest` runs without a visible window and without grabbing focus, producing the same
pass/fail as a windowed run.

---

### Phase 6 — Test corpus + CI wiring

The script corpus lives in `tests/e2e/*.txt`. The fully-calibrated, working scenario is
`load_save_to_mapscreen.txt` (boot → Continue Saved Game → load → assert the strategic world map).
Alongside it are calibration/dev helpers (`smoke`, `calibrate`, `probe`, `driver_selftest`). For
the reusable, calibrated building blocks (reach the main menu, load a save, anchor pixels, how to
calibrate new anchors) see [tests/e2e/COOKBOOK.md](../../tests/e2e/COOKBOOK.md) rather than
duplicating coordinates here.

`CMakeLists.txt` globs `tests/e2e/*.txt` into `ctest` cases (gated behind `WITH_UNITTESTS`) and
adds a `run-ja2-e2e` convenience target:
```cmake
file(GLOB E2E_FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/*.txt")
foreach(f ${E2E_FILES})
  get_filename_component(n ${f} NAME_WE)
  add_test(NAME e2e_${n}
           COMMAND "$<TARGET_FILE:${JA2_BINARY}>" -uitest ${f}
           WORKING_DIRECTORY "$<TARGET_FILE_DIR:${JA2_BINARY}>")
endforeach()
```

**Done:** `ctest -R e2e` runs every script and fails on any pixel assertion.

> **Not yet done:** CI artifact archiving of `screenshot` output on failure. Scripts depend on
> game data at `~/Workspace/ja2-gamedir/app` and assume `-res 1280x720`, so they are not yet wired
> into headless CI. Note also that `driver_selftest.txt` *intentionally* asserts on out-of-bounds
> pixels and therefore exits non-zero by design — treat it as a manual driver check, not a
> green-by-default ctest case.

---

## Script format (full reference)

Plain text, one command per line, `#` starts a comment, blank lines ignored.

| Command | Args | Meaning |
|---|---|---|
| `move` | `X Y` | Move cursor to logical pixel |
| `click` | `X Y` | Left press+release at (X,Y) |
| `rclick` | `X Y` | Right press+release |
| `key` | `NAME` | Press+release a key (`ESCAPE`,`ENTER`,`F1`,`A`…) |
| `type` | `text...` | Send text input (name fields) |
| `wait` | `MS` | Sleep milliseconds |
| `waitpixel` | `X Y #RGB [tol] [timeoutMs]` | Block until pixel matches (defaults: tol 8, timeout 30000) |
| `waitstable` | `X Y [timeoutMs]` | Block until pixel unchanged for 3 frames (default timeout 10000) |
| `assertpixel` | `X Y #RGB [tol]` | Pass/fail assert on a pixel (default tol 8) |
| `screenshot` | `path` | Dump current frame as `.bmp` + `.jpg` (extension on `path` is stripped) |

The `loadsave` shortcut sketched in the original design was **not** implemented — scripts reach a
save via the normal menu flow (see `load_save_to_mapscreen.txt` / the COOKBOOK).

### Example scripts

Rather than inline a scenario here (coordinates drift with UI art and resolution), see the
calibrated, working scripts in `tests/e2e/` — `load_save_to_mapscreen.txt` is the canonical
end-to-end example — and the reusable building blocks documented in
[tests/e2e/COOKBOOK.md](../../tests/e2e/COOKBOOK.md).

---

## Files (new / changed)

| File | Change |
|---|---|
| `src/sgp/SGP.cc` | Strip/parse `-uitest`; load+`pump()` the driver in `MainLoop()`; `uitestSetExitCode()`; `SDL_HINT_MAC_BACKGROUND_APP` |
| `src/sgp/UITestDriver.h/.cc` | **New.** Parse script, inject events, run asserts, screenshots, set exit code |
| `src/sgp/stb_image_write.h` | **New (vendored).** Single-header JPEG writer for `screenshot` |
| `src/sgp/Video.cc` / `Video.h` | `GetScreenBufferForTest()` accessor; `SDL_WINDOW_HIDDEN` in `-uitest` mode |
| `src/sgp/CMakeLists.txt` | Add `UITestDriver.cc` to the build |
| `tests/e2e/*.txt`, `README.md`, `COOKBOOK.md` | Scenario scripts + calibration docs |
| `CMakeLists.txt` | `ctest` e2e cases + `run-ja2-e2e` target (gated behind `WITH_UNITTESTS`) |

> Note: `src/sgp/Input.h` was **not** changed — the cursor is mirrored into `gusMouseXPos/YPos`
> via inline `extern` declarations in `UITestDriver.cc`.

---

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Coordinates drift when UI art/layout changes | Keep scripts few and high-value; prefer `waitpixel` anchors over many absolute clicks; one calibration screenshot per scenario |
| Exact-color asserts are brittle (dithering) | Mandatory tolerance, default 8; assert on stable solid-color regions (HUD panels), not textured art |
| Animation/load timing varies | `waitpixel`/`waitstable` instead of fixed `wait`; generous timeouts that *fail*, don't hang forever |
| Retina/window-chrome offsets (Phase 0 only) | In-process path reads logical `ScreenBuffer` — no offset; Phase 0 documents the 2× caveat |
| Click latch timing (down/up too fast) | Insert a one-pump gap between button down and up |
| Save-file format changes break fixtures | Pin fixture saves to a version; note it at the top of each script |
| Hidden-window texture ops crash (Phase 5) | In practice the default renderer populates the CPU `ScreenBuffer` fine with `SDL_WINDOW_HIDDEN`; the planned software-renderer fallback was not needed |

---

## Build order (as built)

Phase 0 was skipped. Phases 1–3 (flag, input injection, pixel asserts) landed as the minimum
useful harness, Phase 4 added the `waitpixel`/`waitstable` sync helpers, and Phases 5–6 added the
always-hidden window and `ctest` wiring. The canonical working scenario is
`load_save_to_mapscreen.txt`; see [tests/e2e/COOKBOOK.md](../../tests/e2e/COOKBOOK.md) for reusable
fragments.
