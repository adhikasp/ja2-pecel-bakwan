# Headless, Scriptable, Multi-Session JA2 — Design

> **Status: phases 1–3 implemented** (without the MCP server, which was dropped in favour of the
> `ja2ctl` CLI). User guide: [docs/automation.md](../automation.md). Where the implementation
> differs from the proposal below:
>
> - **No `Host` interface.** The seam is smaller: `sgp::Clock` (one clock, real or virtual),
>   `sgp::SetHeadless()`, and `sgp::StepFrame()` / `sgp::DispatchInputEvent()` in SGP.cc. That
>   covered every case without an extra abstraction layer.
> - **Flags** follow the existing single-dash style: `-run`, `-serve`, `-load`, `-seed`, `-home`,
>   `-show`, ... `--new-game` and `--screen` were not needed: `tests/e2e/lib/campaign.lua` gets
>   there through the real UI in about a second of wall-clock time.
> - **`-serve` is TCP only.** The game's logger writes to stdout, so stdio is not a clean channel.
> - **Finding things** combines three sources: button text, captions recorded by the text registry
>   (`src/sgp/TextRegistry.*`, which also powers `texts()`), and `SetName()` for image-only widgets
>   (instead of the proposed `debugName`).
> - **Idle** is structural only (screen/fade/laptop/AIM/dialogue/helicopter/walking predicates
>   held for 3 frames); pixel stability turned out to be unnecessary.
> - The `UITestDriver` was replaced rather than kept; `.txt` scripts are translated to Lua.
> - Phase 4 (input recording and replay) is not implemented. The determinism check is
>   `tests/e2e/check_determinism.py`.

## Goal

Run the real game — same C++ code, same screens, same rules — with no window, no audio device,
and no human, driven from a CLI or a program:

```bash
ja2 run   tests/e2e/hire_merc.lua --load "SaveGame03" --seed 42 --out out/     # batch
ja2 serve --stdio --load "SaveGame03" --seed 42                                # interactive (JSON-RPC)
ja2ctl spawn -n 8 --load SaveGame03 -- run fuzz_mapscreen.lua                  # N parallel sessions
```

Every UI component must be reachable: main menu, load/save, strategic map, laptop (email, AIM,
IMP, finances…), tactical, shopkeeper, auto-resolve, message boxes.

## What the engine does today (and why it's in the way)

| Concern | Today | Problem for headless/CLI |
|---|---|---|
| Main loop | `MainLoop()` in [SGP.cc](../../src/sgp/SGP.cc) owns the process: polls SDL, sleeps to 144 Hz, runs `GameLoop` | Nobody else can drive it; always wall-clock paced |
| Time | Three independent wall clocks: `SDL_GetTicks` (`GetClock()` in [Timer.h](../../src/sgp/Timer.h)), `steady_clock` (`ReferenceClock` in [Timer_Control.h](../../src/game/Utils/Timer_Control.h), [FPS.cc](../../src/sgp/FPS.cc), `RefreshScreenCapped` in [Video.cc](../../src/sgp/Video.cc)), plus `SDL_Delay` in FOV/MapUtility | Can't fast-forward, can't pause while an agent thinks, not deterministic |
| Modal loops | Laptop power-up ([Laptop.cc:1009](../../src/game/Laptop/Laptop.cc)), auto-resolve ([Auto_Resolve.cc:392](../../src/game/Strategic/Auto_Resolve.cc)), pre-battle, sector transition ([StrategicMap.cc:209](../../src/game/Strategic/StrategicMap.cc)) busy-wait on `GetClock()` inside their own loop | Any virtual clock that only advances in `MainLoop` deadlocks here |
| Video | SDL window + renderer + streaming texture; the real frame is the CPU `ScreenBuffer` (RGB565) | Needs a display/GPU; hidden-window trick still creates one |
| Audio | SDL audio device pulls the miniaudio mixer on its own thread | Dialogue/faces/speech advance on `SoundIsPlaying()` ([Dialogue_Control.cc](../../src/game/Tactical/Dialogue_Control.cc), [Faces.cc](../../src/game/Tactical/Faces.cc)) — with `-nosound` or a dummy device, timing diverges from real play |
| RNG | Seeded from `system_clock` + `random_device` ([Random.cc](../../src/sgp/Random.cc)) | Not reproducible |
| Input | SDL events → `Input.cc` handlers; some code reads `SDL_GetModState`/mouse state directly (8 sites) | Injection must go through SDL's queue; modifier state leaks from the real keyboard |
| Per-process state | Home dir (`%APPDATA%\JA2`), `ja2.json`, settings written on exit, fixed `ja2.log` | Parallel sessions stomp on each other |
| Observability | Pixels only | Brittle coordinates; no way to ask "which screen am I on / what buttons exist / is the game idle" |

The engine is ~all globals. **Making it re-entrant for multiple in-process sessions is not
worth it.** One session = one process, and the design makes processes cheap and isolated.

## Architecture

```
          ┌─────────────── front-ends ────────────────┐
          │ ja2 run x.lua │ ja2 serve (JSON-RPC) │ .txt │  ← legacy UITestDriver DSL
          └───────┬───────┴──────────┬───────────┴──┬───┘
                  ▼                  ▼              ▼
          ┌──────────────── Automation core (src/sgp/automation/) ─────────────┐
          │ Session: step(), input, query UI tree, waits, screenshot, save/load │
          │ Lua binding "ja2.*"  ·  command queue  ·  settle detector            │
          └───────┬──────────────────────────────────────────────┬─────────────┘
                  ▼                                              ▼
          ┌──── Host (platform seam) ────┐              ┌── Introspection ──┐
          │ Clock · EventSource ·        │              │ MOUSE_REGION /     │
          │ Presenter · AudioSink        │              │ GUI_BUTTON registry│
          │  SdlHost  |  HeadlessHost    │              │ screen/game state  │
          └──────────────────────────────┘              └────────────────────┘
                  ▼
          unchanged game: GameLoop(), screens, tactical, strategic, laptop
```

### 1. The Host seam (`src/sgp/Host.h`)

A small interface the SGP layer calls instead of SDL directly:

```cpp
namespace sgp {
struct Host {
    virtual ~Host() = default;
    // time
    virtual uint32_t ticksMs() = 0;                 // replaces SDL_GetTicks in GetClock()
    virtual void     sleepMs(uint32_t) = 0;         // replaces SDL_Delay / sleep_until
    virtual void     yield() = 0;                   // "a frame was presented outside MainLoop"
    // input
    virtual bool     pollEvent(SDL_Event&) = 0;
    virtual SDL_Keymod modState() = 0;
    // output
    virtual void     present(SDL_Surface* screenBuffer, SDL_Rect dirty) = 0;
    virtual void     pumpAudio(uint32_t elapsedMs) = 0;
};
Host& host();
}
```

- **`SdlHost`** — today's behaviour, byte-for-byte: real clock, SDL window/renderer, SDL audio.
- **`HeadlessHost`** — no window, no renderer, no audio device (`SDL_Init(SDL_INIT_EVENTS)`
  only, or nothing). Virtual clock. Events come from the automation core. `present()` just
  records the dirty rect and bumps a frame counter; `ScreenBuffer` is already a CPU surface so
  screenshots and pixel reads work unchanged.

Selected by `--headless` (implied by `run`/`serve`). Everything above the seam is identical in
both modes — that's the property that makes headless results trustworthy.

### 2. One virtual clock

Unify all three clocks behind `sgp::Clock`:

- `GetClock()` → `host().ticksMs()`.
- `ReferenceClock` becomes a custom chrono clock (`struct sgp::GameClock { static time_point now(); … }`)
  instead of `std::chrono::steady_clock`. `TIMECOUNTER`, `UpdateJA2Clock`, `FPS`, and
  `RefreshScreenCapped` switch over by changing one `using`. Surgical, and it keeps the
  type-safe chrono API.
- `SDL_Delay` / `sleep_until` → `host().sleepMs()` (headless: advances virtual time, returns immediately).

**Stepping model (headless).** Time advances only when the driver says so:
`step(frames=N)` runs N iterations of `GameLoop()` with a fixed quantum (default 1000/60 ms
— configurable; 144 Hz like `MainLoop` is also fine). Between steps the world is frozen, so an
agent can think for a minute without the game moving. `run` mode steps as fast as the CPU allows
— loading a save to the map screen should drop from ~15 s wall-clock to well under a second of
CPU plus load I/O.

**Modal loops.** The busy-wait loops call `RefreshScreen()` each iteration. `RefreshScreen()`
calls `host().yield()`; headless `yield()` advances the virtual clock one quantum (and pumps
audio). Belt and braces: `ticksMs()` counts calls since the last advance and, past a threshold
(e.g. 10 000), advances 1 ms and logs a warning — so an unanticipated busy-wait degrades to
"slow" rather than "hang". A wall-clock watchdog (`--watchdog 60s`) kills a truly stuck session
with a screenshot + log.

### 3. Audio that keeps real timing, without a device

Keep the whole SoundMan/miniaudio mixing path; only replace who *pulls* samples. Headless
`pumpAudio(elapsedMs)` pulls `elapsedMs × rate` frames through the same mix function the SDL
callback uses and discards them — or writes them to `out/session.wav` with `--record-audio`.
Consequence: `SoundIsPlaying()` goes false at exactly the virtual moment it would in a real run,
so speech, face animation, dialogue queues and IMP voice previews behave identically. The
`SoundServiceBuffers` thread is replaced by synchronous servicing in `pumpAudio` (also removes a
source of nondeterminism).

`-nosound` stays for people who truly want silence-and-skip semantics.

### 4. Determinism

- `--seed N` seeds `gRandomEngine` and the pre-random table; no `system_clock`/`random_device`.
  Default in headless mode is a fixed seed (printed in the log) so every run is reproducible.
- Virtual clock + synchronous audio + injected input ⇒ same save + same seed + same script =
  same frames. This is what makes golden screenshots and bisecting possible.
- `SDL_GetModState`/`SDL_GetMouseState` sites go through the host so the real keyboard can't leak in.
- Lua: nothing time- or entropy-based is exposed.

A CI check (`ja2 run determinism_check.lua` twice, compare frame hashes) guards this.

### 5. Boot straight into any state

New CLI (parsed in C++ before `EngineOptions_create`, as `-uitest` is today — or better, added
to the Rust CLI in `engine_options.rs` so `--help` lists them):

| Flag | Effect |
|---|---|
| `--headless` | HeadlessHost |
| `--seed N` | deterministic RNG |
| `--home DIR` | override Stracciatella home (config, settings, default save dir) |
| `--save-dir DIR` | override save dir (already exists in ja2.json; expose on CLI) |
| `--log FILE` | per-session log path |
| `--load NAME\|PATH` | after `InitializeGame()`, skip intro/menu, call `LoadSavedGame()` and go to `guiScreenToGotoAfterLoadingSavedGame` — the same path [SaveLoadScreen.cc:1184](../../src/game/SaveLoadScreen.cc) takes |
| `--new-game key=val,…` | start a new campaign with given options (difficulty, iron man, sci-fi…), optionally a canned IMP profile, landing on map screen |
| `--screen map\|laptop\|tactical` | after boot/load, switch screen (laptop = `LAPTOP_SCREEN` entry path) |
| `--no-intro` | skip splash and cinematics (implied by `--load`/`--new-game`) |
| `--quantum-ms N` | virtual frame length |

Loading a save deliberately goes through the **real** load code, not a shortcut, so "load then
play" tests the same thing a player does.

### 6. Seeing the UI as data, not pixels

Pixels stay available, but the primary way to find things becomes the game's own UI registry:

- **`MOUSE_REGION` list** ([MouseSystem.h](../../src/sgp/MouseSystem.h)) and **`GUI_BUTTON`
  table** ([Button_System.h](../../src/sgp/Button_System.h)) already hold every clickable
  rectangle, its priority, enabled flag, button text (`codepoints`) and fast-help text.
  `ui.tree()` walks them and returns
  `{id, kind, rect, enabled, text, help, priority, name}` — sorted by what would actually receive
  the click (priority + z-order, same rule `MSYS` uses for hit-testing).
- **Names for the unnamed.** Many regions have no text (map sectors, inventory slots, laptop
  icons). Add an optional `const char* debugName` to `MOUSE_REGION`/`GUI_BUTTON` and set it at
  creation sites incrementally (`"laptop.icon.email"`, `"map.sector.A9"`, `"tactical.merc.3"`).
  Zero runtime cost, grows as tests need it. Sector/grid-based widgets get helper locators
  instead (`map.sector("A9")`, `tactical.gridno(12345)` → screen coords via the existing
  world→screen transforms).
- **Screen & modal state:** `guiCurrentScreen`, pending screen, message box open + its text and
  buttons, laptop current page, fade in progress, dialogue queue length, tactical turn/team.
- **Settle detector** — `waitIdle()` returns when: no pending screen, no fade, no message box
  animating, dialogue queue empty, no soldier animating/moving, no scroll, and N consecutive
  frames with an empty dirty-rect set. This replaces almost every `wait 3000` in today's scripts.

Clicking by locator still injects a real mouse move + press + release at the region's center,
so hit-testing, hover states and callbacks are exercised exactly as for a player. There is also
a `ui.invoke(id)` escape hatch that calls the callback directly, clearly marked as not-E2E.

### 7. The automation API (Lua, in-process)

sol2 is already embedded ([ScriptingExtensionsLua.cc](../../src/externalized/scripting/ScriptingExtensionsLua.cc)),
so test scripts are Lua running in a **separate `sol::state`** from mod scripts (mods can't see
it, it can't break mods). Scripts run as coroutines: every wait/step yields back to the engine.

```lua
-- tests/e2e/hire_ivan.lua   (ja2 run tests/e2e/hire_ivan.lua --load Day1)
local ui, game = ja2.ui, ja2.game

ja2.expect(game.screen()).toBe("MAP_SCREEN")
ui.key("L")                                   -- open laptop
ui.waitScreen("LAPTOP_SCREEN")
ui.click{ name = "laptop.icon.web" }
ui.click{ text = "A.I.M." }
ui.waitIdle()
ui.click{ name = "aim.members" }
ui.click{ name = "aim.face", merc = "IVAN" }
ui.click{ text = "CONTRACT" }
ui.click{ text = "1 WEEK" }
ui.click{ text = "TRANSFER FUNDS" }
ja2.step{ until_ = function() return game.merc("IVAN").onTeam end, timeout = "2h" } -- game time
ja2.screenshot("ivan_hired.png")
ja2.expect(game.balance()).toBeLessThan(45000)
```

API surface (sketch):

- `ja2.step{frames=, ms=, until_=, timeout=}` — advance virtual time.
- `ja2.ui`: `move/click/rclick/drag/key/type/wheel`, locators (`{name=}`, `{text=}`,
  `{help=}`, `{x=,y=}`), `tree()`, `find()`, `waitScreen()`, `waitIdle()`, `waitFor(pred)`,
  `pixel(x,y)`, `messageBox()`.
- `ja2.game` (read-mostly): `screen()`, `time()`, `balance()`, `sector()`, `mercs()`,
  `merc(name)`, `squad()`, `tacticalStatus()`, `laptopPage()`, `facts`, `quests`.
  Implemented so far: `ja2.game.npcs()` and `ja2.game.quests()`, the native people
  & quest registry (issue #156, [living-world-npc.md](living-world-npc.md)).
- `ja2.save(name)`, `ja2.load(name)` — through the real save/load code.
- `ja2.screenshot(path)`, `ja2.record{frames=dir}` (PNG sequence / pipe to ffmpeg), `ja2.expect`.
- `ja2.cheat.*` (explicitly separate namespace): set money, teleport squad, spawn item, reveal
  map — for setting up scenarios fast, never used by "pure" E2E tests.

The legacy `.txt` DSL is kept by compiling each line to the equivalent Lua call, so existing
scripts and `COOKBOOK.md` stay valid.

### 8. `ja2 serve`: drive a live session from outside

Newline-delimited JSON-RPC 2.0 on stdin/stdout (logs go to the log file, never stdout). Optional
`--listen tcp:127.0.0.1:0` / named pipe for tools that prefer sockets. The method set is the Lua
API 1:1, plus `eval` (run a Lua chunk) — one implementation, two transports.

```json
→ {"id":1,"method":"step","params":{"until":"idle","timeout_ms":20000}}
← {"id":1,"result":{"frame":812,"virtual_ms":13533,"screen":"MAP_SCREEN"}}
→ {"id":2,"method":"ui.tree","params":{"visible":true}}
← {"id":2,"result":[{"id":"b17","kind":"button","text":"Laptop","rect":[...],"enabled":true}, ...]}
→ {"id":3,"method":"screenshot","params":{"format":"png","inline":true}}
← {"id":3,"result":{"png_base64":"iVBOR..."}}
```

In serve mode the game is frozen between requests (virtual time), which is what an LLM agent or
a Python test harness wants. Screenshots returned inline (base64 PNG) or as file paths.

On top of this, thin clients:

- **`ja2ctl`** (Python, `tools/ja2ctl/`): spawn/list/kill sessions, run a script across N
  sessions, collect results into `out/<session>/{log,screens,result.json}`.
- **MCP server** (`tools/ja2-mcp/`): wraps `ja2ctl` so Claude can `session.start(load=…)`,
  `ui.tree`, `click`, `screenshot`, `step` directly.

### 9. Multiple sessions

One process per session, isolated by construction:

- `--home`, `--save-dir`, `--log` per session (`ja2ctl` creates `sessions/<id>/…`); temp dir is
  already per-process (`TempDir_create`).
- In headless mode, `SaveGameSettings()` on exit writes to the session home, never the user's.
- Game data (`.slf`, externalized JSON) is read-only and shared; the OS page cache makes N
  sessions cheap on disk I/O. Expect memory per session in the low hundreds of MB — measure in
  Phase 1.
- No window, no audio device, no focus ⇒ nothing global to contend for. `ctest -j16` just works.
- **Forking a session** = `ja2.save("fork")` then spawn new sessions with `--load fork`. Cheap
  enough to do exploratory "branch here and try 8 things" workflows.

### 10. Failure behaviour

Headless must never block on a human:

- `gfGlobalError`/`ERROR_SCREEN`, uncaught exceptions, assertion failures → dump screenshot,
  UI tree, last 200 log lines to `out/`, exit non-zero (`1` = test failure, `2` = setup/script
  error, `3` = game error, `4` = watchdog timeout).
- Unexpected message box → recorded in the result; scripts can `ui.messageBox():accept()` or
  opt into `ja2.autoDismiss{...}`.
- `result.json` per run: exit reason, frames, virtual ms, wall ms, seed, screenshots, assertions.

## Phased plan

Each phase ships something usable on its own.

**Phase 1 — Headless core (the foundation).**
Host seam; `SdlHost` preserving current behaviour; `HeadlessHost`; unified virtual clock
(`GetClock`, `ReferenceClock`, sleeps); `yield()` in `RefreshScreen`; synchronous audio pump;
`--headless --seed --home --save-dir --log --load --no-intro`. Existing `UITestDriver` runs on it.
*Done when:* `load_save_to_mapscreen.txt` passes with `--headless`, with no window created, 10×
faster than today, identical screenshot hash across two runs, and four copies in parallel.

**Phase 2 — Introspection + Lua automation.**
`ui.tree()` over regions/buttons, `debugName` on the first ~50 creation sites (main menu,
load screen, map screen bottom panel, laptop icons, message boxes), settle detector, `ja2 run
x.lua`, `.txt`→Lua shim, `result.json`. Port the four existing e2e scripts to Lua with no pixel
coordinates.
*Done when:* a Lua script goes save → map → laptop → AIM → hire merc → back to map → tactical
→ move merc, locators only.

**Phase 3 — `serve` + clients.**
JSON-RPC over stdio, `ja2ctl` with multi-session spawn, MCP server.
*Done when:* an agent can start two sessions from the same save, drive them independently, and
fetch screenshots.

**Phase 4 — Determinism hardening + record/replay.**
Frame-hash determinism CI, `--record-input trace.jsonl` in the normal SDL game (input events
stamped with frame numbers), `--replay trace.jsonl` headless. Turns any bug a human hits into a
reproducible headless test.

## Risks / open questions

- **Hidden wall-clock dependencies** beyond the ones grepped (music streaming, video/SMK
  cinematics, fades). Mitigation: headless build logs any direct `SDL_GetTicks`/`steady_clock`
  use via a lint grep in CI; cinematics skipped in headless by default.
- **Audio pump cost** when stepping thousands of frames per second — mixing is cheap, but
  measure; offer `--audio=timing-only` that advances sample cursors without decoding if needed.
- **Editor** (`EditScreen`) has its own `DequeueEvent` loops — out of scope for now.
- **Android** keeps `SdlHost`; nothing changes there.
- Upstream-friendliness: the Host seam and clock unification are good upstream candidates
  independently of the automation layer; keep them in separate commits.
