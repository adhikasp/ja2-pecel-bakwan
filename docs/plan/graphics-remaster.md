# Graphics Remaster — Modern Resolutions, HUD Scaling, Widescreen

> **Status: proposal.** Nothing here is implemented yet. This is the first step of the wider
> remaster. Gameplay, data formats and save games stay as they are.

## Goal

Make the game look right on a modern display (1080p, 1440p, 4K, 16:10, 21:9 and 32:9, HiDPI
laptops) without changing how it plays:

1. **Modern resolutions.** Default to the desktop's native size and aspect ratio. No black
   letterbox around a 640x480 or 1024x768 image.
2. **HUD scaling.** The UI (panels, text, buttons, cursor, popups) is drawn at an integer
   **UI scale** (1x, 2x, 3x, 4x) and stays sharp. The tactical world has its own **world zoom**,
   so a 1440p player can use a 2x UI and still see more of the map at 1x world zoom.
3. **Widescreen.** Screens that are hard-wired to 640x480 (map screen, laptop, menus) use the
   extra width properly or are framed well. They should not float as a small box in the middle.
4. **Keep what works.** Headless automation, e2e tests, the editor, mods and the 640x480
   "classic" mode all keep working. Every phase is shippable by itself.

Out of scope for this initiative: new art, HD sprite packs, 32-bit colour, a GPU tile renderer
and rebalancing. Phase 6 lists these as follow-ups so that the earlier phases don't block them.

---

## Where things stand today (research)

### The render pipeline

```
 game code ──draws──▶ FRAME_BUFFER (SDL_Surface, RGB565, SCREEN_WIDTH x SCREEN_HEIGHT)
                        ▲   ▲
     guiSAVEBUFFER ─────┘   └── RestoreBackgroundRects / video overlays / dirty rects
     (background copy)
                        │ RefreshScreen(): copy dirty rects + scroll strips
                        ▼
                  ScreenBuffer (RGB565) + software MouseCursor (64x64) blitted on top
                        │ SDL_UpdateTexture (dirty region only)
                        ▼
                  ScreenTexture ──SDL_RenderTexture──▶ window
                  (logical presentation: LETTERBOX / INTEGER_SCALE / 4x oversample)
```

- **`src/sgp/Video.cc` (680 LOC).** Owns the window, the renderer, `ScreenTexture`, the
  optional 4x `ScaledScreenTexture`, `FrameBuffer`, `ScreenBuffer` and `MouseCursor`.
  `SDL_SetRenderLogicalPresentation(SCREEN_WIDTH, SCREEN_HEIGHT, …)` does **all** of the scaling
  today. The whole frame (world and UI together) is scaled by a single factor that SDL picks
  from the window size. The scroll optimisation in `RefreshScreen()` shifts the framebuffer
  in place and redraws only the exposed strips.
- **Scaling modes** (`rust/pecel_bakwan/src/config/scaling_quality.rs`): `LINEAR`,
  `NEAR_PERFECT` (nearest up to 4x, then linear down) and `PERFECT` (integer scale only).
- **Resolution** (`rust/.../config/resolution.rs`) defaults to `640x480`. It is read once in
  `src/sgp/SGP.cc:426` → `g_ui.setScreenSize()` → `g_ui.recalculatePositions()` →
  `InitializeVideoManager()`. There is no runtime resolution change, and window resizes only
  rescale the image.
- **`src/sgp/VSurface.*`** — `SGPVSurface` wraps an `SDL_Surface`. Game surfaces
  `guiSAVEBUFFER` and `guiEXTRABUFFER` are created at `SCREEN_WIDTH x SCREEN_HEIGHT` in
  `src/game/TileEngine/SysUtil.cc`. `gpZBuffer` is created in `GameScreen.cc:82`.
- **`src/sgp/VObject_Blitters.cc` (3.1k LOC, 42 blitters).** These are 8-bit palettised
  STI → RGB565 blitters with Z-buffer, shadow, translucency and outline variants. None of them
  scale. `BltStretchVideoSurface` exists and is used in about 10 places for zoom animations
  (laptop open/close, PBI, auto-resolve).
- **Fonts** (`src/sgp/Font.cc`, `src/game/Utils/Font_Control.cc`) are 16 bitmap `.sti` fonts.
  They have no scaling or hinting.
- **Input** (`src/sgp/Input.cc`) takes SDL mouse coordinates already converted to render
  (logical) space and writes them to `gusMouseXPos/gusMouseYPos`. That is 108 uses in 23 files.
  There are about 1,000 `MSYS_DefineRegion`/`MOUSE_REGION` uses, all in logical pixels.

### The layout system

- **`src/game/UILayout.{h,cc}`** is the global `g_ui`. It has a minimum size of 640x480 and
  exposes `SCREEN_WIDTH/HEIGHT`, the tactical viewport (`gsVIEWPORT_*`), the team-panel position
  (centred along the bottom, 6–12 slots depending on width), the inventory slot tables, and
  `STD_SCREEN_X/Y`. `STD_SCREEN_X/Y` is the offset of a centred 640x480 box.
- The **tactical screen** already adapts to the resolution. The world viewport fills the
  screen minus the 120 px bottom bar. The bottom bar is centred and the side gaps are filled
  black (`FillEmptySpaceAtBottom`, `Interface_Panels.cc:852`).
- **Everything else is a centred 640x480 box.** `STD_SCREEN_X` has 353 hits and
  `STD_SCREEN_Y` has 404 hits across about 54 files. The heaviest users:

  | File | STD_SCREEN_X hits |
  |---|---|
  | `Strategic/MapScreen.cc` (8k LOC) | 50 |
  | `Laptop/Laptop.cc` + ~25 laptop pages | ~140 |
  | `TileEngine/Tactical_Placement_GUI.cc` | 22 |
  | `Strategic/PreBattle_Interface.cc` | 21 |
  | `Strategic/Map_Screen_Interface_Bottom.cc` | 21 |
  | `Options_Screen.cc`, `SaveLoadScreen.cc`, `GameInitOptionsScreen.cc`, `Credits.cc`, `Intro.cc`, `MainMenuScreen.cc` | ~40 |

  Also: `SCREEN_WIDTH` has 210 hits in 64 files and `SCREEN_HEIGHT` has 143 hits in 56 files.
  Literal `640` appears 34 times and `480` 28 times; each one needs checking.

### Tactical frame order (`GameScreen.cc:~475`)

`RenderWorld()` → `RenderTopmostTacticalInterface()` (names, HP bars, cursor text, bottom panel)
→ `RenderRadarScreen()` → `ExecuteVideoOverlays()` → `RenderButtons()` →
`SaveBackgroundRects()` → fast help. **The world and the UI draw into the same `FRAME_BUFFER`,
and `guiSAVEBUFFER` holds both.** UI shadows (`ShadowVideoSurfaceRect`, 31 calls) darken the
world pixels underneath them. This shared framebuffer is the main obstacle to separate UI and
world scales.

### What this means

| Want | Today | Gap |
|---|---|---|
| 4K / 1440p / ultrawide window | `-res 2560x1080` works, but the user must pick it and non-tactical screens are a small centred box | Auto-resolution, scale-aware logical size, widescreen layouts |
| Readable HUD at 4K | Only by lowering the logical res (scales the world too) | Separate UI layer with its own scale |
| Crisp at non-integer ratios | `NEAR_PERFECT` oversampling (4x texture, costly at 4K) | Sharp-bilinear present path |
| Change resolution in-game | Not possible | Rebuild surfaces and layout at runtime |

---

## Target architecture

```
                 world zoom Zw                      UI scale Su
 RenderWorld ─▶ WORLD layer (RGB565)       UI code ─▶ UI layer (RGB565 + alpha/mask)
   size = window / Zw                           size = window / Su  ("UI logical size")
        │                                             │
        └────────── Compositor (Video.cc, GPU) ◀──────┘   + cursor layer at Su
                    world tex × Zw, UI tex × Su, nearest / sharp-bilinear
```

Key decisions:

1. **Keep the software renderer and RGB565.** All 42 blitters, the Z-buffer, palettes and
   shading tables stay as they are. The GPU is used only to **composite and scale** layers.
   This keeps the risk low and keeps headless mode working, because headless mode never touches
   the GPU.
2. **The UI logical size is the new `SCREEN_WIDTH/HEIGHT`.** All existing UI code (1,000 mouse
   regions, hardcoded offsets) keeps working in "UI pixels". `SCREEN_WIDTH = window_w / Su`,
   with a minimum of 640x480. The UI scale is clamped so that the minimum always fits. For
   example, 3840x2160 at 3x gives 1280x720 UI pixels.
3. **The world gets its own surface only on tactical-type screens** (tactical, overhead map,
   editor). All other screens stay single-layer and are scaled by `Su`.
4. **Two coordinate spaces:** *UI space* (mouse regions, buttons, all current code) and *world
   space* (`RenderWorld`, Z-buffer, tile picking). One conversion point,
   `UiToWorld()`/`WorldToUi()`, lives in `UILayout`.
5. **Integer scales first.** Fractional UI scale (1.5x) comes later through sharp-bilinear
   presentation, because pixel art looks bad with plain linear filtering.

---

## Phases

Rough size is changed lines of C++/Rust, excluding tests. The total is roughly **12–20k LOC**,
so the work is split into six phases, each mergeable by itself.

| # | Phase | Size | Risk | User-visible result |
|---|---|---|---|---|
| 0 | Baseline and resolution test matrix | ~0.5k | low | none (safety net) |
| 1 | Modern display and uniform scale | ~1.5–2k | low | Native-res fullscreen, crisp integer scaling, HiDPI |
| 2 | Layout anchoring and widescreen tactical HUD | ~2–3k | medium | Tactical HUD uses the full width and anchors to edges |
| 3 | Widescreen strategic and menu screens | ~4–6k | medium | Map screen, laptop and menus fill 16:9/21:9 |
| 4 | Layer split: independent UI scale and world zoom | ~3–5k | **high** | HUD 2x, world 1x (or any mix) |
| 5 | Runtime video settings | ~1–2k | medium | Change resolution/scale/zoom in-game |
| 6 | Follow-ups (HD fonts, 32-bit, HD asset overrides) | separate | — | — |

Phases 1 → 2 → 3 are strictly incremental. Phase 4 can start after Phase 2, in parallel
with 3, because it touches the compositor and tactical code, not the strategic screens.

---

### Phase 0 — Baseline and resolution test matrix

Refactoring 50+ files of hand-placed coordinates without visual regression tests is unsafe.
This phase adds them.

- Add `res` and later `ui_scale`/`world_zoom` parameters to the e2e harness
  (`tools/ja2ctl.py` already passes `-res`). Run the existing tours (`main_menu`,
  `map_screen_tour`, `laptop_tour`, `new_game_to_tactical`, `tactical_move_merc`) at
  **640x480, 1280x720, 1920x1080, 2560x1080 and 3440x1440**.
- Add golden screenshots (`ja2ctl shot`) with a perceptual/pixel-diff tolerance under
  `tests/e2e/golden/<res>/`. The virtual clock is deterministic
  (`check_determinism.py`), so golden images are practical.
- Add an automation assertion `ja2.assertInsideScreen()` that walks every active mouse region
  and button and fails if one lies outside `SCREEN_WIDTH/HEIGHT` or overlaps another in a
  suspicious way. This catches most layout bugs without pixels.
- Add a `ctest -L resolution` label.

**Files:** `tests/e2e/*.lua`, `tests/e2e/lib/`, `tools/ja2ctl.py`,
`src/game/Automation/*` (region dump), `CMakeLists.txt` (ctest labels).

**Done when:** the matrix passes on current `master`, and the golden images are the reference.

---

### Phase 1 — Modern display and uniform scale

This phase makes the game fill any display crisply with one scale factor (world and UI
together). It fixes most of the "tiny on 4K" complaint on its own, while touching almost no
game code.

**Config (Rust + C API + launcher)**
- `rust/pecel_bakwan/src/config/`: add `ui_scale: Auto | 1..=4` and
  `window_mode: Windowed | Fullscreen | BorderlessDesktop`. Let `resolution` accept `"auto"`
  (desktop size), which becomes the new default for fresh installs. Keep `640x480`
  available as "Classic".
- Expose these through `rust/pecel_bakwan_c_api/src/c/config.rs` and the CLI (`-uiscale N`,
  `-window-mode`).
- Launcher (`src/launcher/StracciatellaLauncher.fl`, `Launcher.cc`): add a resolution list
  from `SDL_GetFullscreenDisplayModes`, a UI-scale dropdown, and a window-mode radio.

**Video (`src/sgp/Video.cc`, `SGP.cc`)**
- Split the **window size** (physical pixels) from the **logical size** (`SCREEN_WIDTH/HEIGHT`).
  `logical = floor(window / Su)`, clamped to at least 640x480. Auto `Su` is the largest integer
  that keeps the logical size ≥ 1280x720 (or ≥ 640x480 on small displays).
- Use `SDL_WINDOW_HIGH_PIXEL_DENSITY` and `SDL_GetWindowPixelDensity` so that HiDPI macOS and
  Windows displays get real pixels.
- Borderless-desktop fullscreen should be the default rather than exclusive mode changes.
- **Sharp-bilinear presentation.** Nearest-scale to the largest integer multiple ≤ the target
  into an intermediate texture, then linear to the final size. This replaces
  `NEAR_PERFECT`'s fixed 4x oversample, which uses about 530 MB of VRAM traffic per frame at
  4K. Keep `PERFECT` (integer + letterbox) as an option.
- Letterbox leftover pixels (window % Su) with a neutral colour instead of stretching.
- Put the logical-size calculation in a pure function with unit tests
  (`src/sgp/Video_unittest.cc`).

**Also check**
- `IsDesktopLargeEnough()` and minimum window size logic.
- `MAX_CURSOR_WIDTH/HEIGHT` (64): cursor stays in logical pixels, which is fine.
- Headless mode (`sgp::IsHeadless()`): logical size = `-res`, scale is ignored.

**Done when:** a fresh install on a 4K 16:9 monitor opens borderless at 3840x2160 with a
1920x1080 (2x) or 1280x720 (3x) logical canvas, pixel-sharp, and a 3440x1440 ultrawide gets
1720x720 at 2x. The Phase 0 matrix still passes.

---

### Phase 2 — Layout anchoring and widescreen tactical HUD

This phase replaces "centre a 640x480 box" with anchors, starting with the tactical screen,
because players spend the most time there.

**`UILayout` API** (`src/game/UILayout.{h,cc}`)
- Add an anchor helper:
  `SGPPoint g_ui.anchor(Anchor a, UINT16 w, UINT16 h, INT16 dx = 0, INT16 dy = 0)`, where
  `Anchor ∈ {TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight}`.
- Add a `SGPBox g_ui.stdBox()` that replaces ad-hoc `STD_SCREEN_X + n` arithmetic in new code.
- Make `recalculatePositions()` idempotent and callable again at runtime (Phase 5 needs this).
- Add unit tests for anchor math at 640x480, 1280x720 and 2560x1080.

**Tactical screen**
- **Bottom bar** (`Tactical/Interface_Panels.cc`, `Interface.cc`, `UILayout.cc`):
  - Team panel: allow up to `squad_size` slots as today, with left/right **skinned fillers**
    (tiled edge art cut from the existing panel STIs) instead of the black
    `FillEmptySpaceAtBottom`.
  - Single-merc inventory panel: centred, with filler art.
  - Radar and clock (`get_RADAR_WINDOW_X`, `get_CLOCK_X`): anchor to the panel's right edge
    as today, with an option to anchor them to the screen's bottom-right.
- **Message queue** (`Utils/Message.cc`), **top messages / turn bar** (`Interface.cc`
  `HandleTopMessages`), **tactical text boxes** (`getTacticalTextBoxX/Y`), **popup menus**
  (`Interface_Control.cc`, `PopUpBox.cc`), **sector exit menu**, **paused box**: anchor them
  instead of using fixed `110,20` or `STD_SCREEN_*`.
- **Talking faces / dialogue panels** (`Tactical/Faces.cc`, `Interface_Dialogue.cc`,
  `DEFAULT_EXTERN_PANEL_*`): anchor to the viewport, not a 640 box.
- **Overhead map** (`TileEngine/Overhead_Map.cc`): centre the half-size map in the viewport and
  fill the remaining space.
- **Tactical placement GUI** (`TileEngine/Tactical_Placement_GUI.cc`, 22 hits): anchor to the
  bottom.
- **World edge / scroll limits** (`RenderWorld.cc` `ApplyScrollLimits` and related code): at
  very wide viewports the whole sector fits, so clamp and centre instead of jittering.

**Audit list:** every `SCREEN_WIDTH`, `SCREEN_HEIGHT`, literal `640`/`480` and `isBigScreen()`
in `src/game/Tactical/` and `src/game/TileEngine/`.

**Done when:** tactical at 1280x720, 1920x1080 and 2560x1080 logical has no black bars in
the HUD, no clipped popups, and all e2e tactical tests pass. Classic 640x480 is pixel-identical
to the Phase 0 golden images.

---

### Phase 3 — Widescreen strategic and menu screens

This is the biggest mechanical phase. Each screen gets one of three treatments, chosen by
cost:

| Treatment | Meaning | Used for |
|---|---|---|
| **A. Framed** | Keep the 640x480 content centred and dress the side/top margins with background art (blurred/tiled copy of the screen's background or a new frame asset) | Laptop and all its web pages, credits, intro, IMP, Bobby Ray, AIM |
| **B. Re-anchored** | Split the screen into panels and anchor each to an edge; the content stays 1:1 | Main menu, options, save/load, game-init options, message boxes, PBI, auto-resolve |
| **C. Expanded** | Content grows with the screen | Strategic map screen |

**3a. Menus (treatment B)** — `MainMenuScreen.cc`, `Options_Screen.cc`, `SaveLoadScreen.cc`,
`GameInitOptionsScreen.cc`, `MessageBoxScreen.cc`, `Utils/Animated_ProgressBar.cc`
(`m_progress_bar_box`), loading screens (`LoadScreen*`).
Backgrounds are full-screen PCX/STI images. Scale-to-cover them with `BltStretchVideoSurface`
(nearest), then anchor the controls.

**3b. Strategic map screen (treatment C)** — `Strategic/MapScreen.cc` (8k LOC, 50 hits),
`Map_Screen_Interface*.cc` (~11k LOC), `MapScreen.h` (17 hits), `m_mapScreenWidth/Height`
(currently fixed at 640x480).
- Left column (character list, info panel, inventory) is anchored left at native size.
- The **strategic map** area takes the rest of the width and height. The 16x16 sector map art
  is fixed-size, so centre it inside the larger area and fill the margins with the map-border
  art. The existing map zoom (`fZoomFlag`) path already renders at 2x, so in large panels
  default to the zoomed map with scrolling.
- Bottom bar (`Map_Screen_Interface_Bottom.cc`, 21 hits: message log, time compression, money,
  clock) spans the full width, and the message log gets more lines.
- Border buttons (`Map_Screen_Interface_Border.cc`) are anchored to the map area.
- Popups (assignment, contract, squad, train, vehicle, repair: the `m_*Position` in
  `UILayout`) are placed relative to the character row instead of `STD_SCREEN_X + 120`.
- The map inventory (`Map_Screen_Interface_Map_Inventory.cc`) can show more pockets per page
  on wide screens (a gameplay-neutral QoL gain).
- Town/mine info and helicopter ETA popups are anchored to the map area.

**3c. Pre-battle and auto-resolve (treatment B)** — `PreBattle_Interface.cc` (21 hits),
`Auto_Resolve.cc`: centre the panel, keep the zoom animation (`BltStretchVideoSurface`
rects).

**3d. Laptop (treatment A)** — `Laptop/Laptop.cc` and about 25 pages (~140 hits). Wrap the
whole laptop in a framed 640x480 viewport. **Do not re-layout the web pages.** The side
margins get laptop bezel or desk art. This keeps the hit count almost unchanged: only
`Laptop.cc` and `Laptop.h` (`LAPTOP_SCREEN_*`) change.

**3e. Cinematics** — `Intro.cc`, `Utils/Cinematics.cc`, `Credits.cc`: scale Smacker video to
fit (nearest, integer where possible) instead of showing a 640x480 box.

**Done when:** every screen at 16:9 and 21:9 is covered by golden screenshots, has no
unintended black margins, and every button is reachable (Phase 0 region audit).

---

### Phase 4 — Layer split: independent UI scale and world zoom

This is the technically hard phase and the one that enables "HUD 2x, world 1x". It applies
only to tactical-type screens.

**4a. Compositor (`src/sgp/Video.cc`)**
- Replace the single `ScreenTexture` with an ordered list of layers:
  `{ surface, texture, scale, dirty-rect list, blend }`. Layers are `WORLD` (at `Zw`), `UI`
  (at `Su`, blended) and `CURSOR` (at `Su`).
- The UI layer needs transparency. Keep RGB565 for the blitters and add a parallel **8-bit
  coverage mask** or a reserved colour key. Before upload, convert the dirty rect to an
  ARGB8888 streaming texture. UI shadows (`ShadowVideoSurfaceRect`, 31 calls) become "write
  50% black with alpha" on the UI layer instead of darkening underlying pixels.
- Dirty-rect upload per layer (keep the current "update only the dirty region" behaviour).
- Scroll optimisation (`ScrollJA2Background`) only applies to the world layer, which is
  simpler because the UI no longer needs the strip re-blits.

**4b. World surface and coordinates**
- Add a new `WORLD_BUFFER` surface sized `window / Zw`. Move `gsVIEWPORT_*`,
  `m_worldClippingRect`, `m_tacticalMapCenter*` and `gpZBuffer`/`gZBufferPitch` to
  **world** size.
- `RenderWorld.cc` (3.6k LOC): change the render target from `FRAME_BUFFER` to `WORLD_BUFFER`,
  including the `SetFontDestBuffer` calls at 1167/3077/3151 and the `SCREEN_WIDTH` Z-buffer
  loops at 1546/1688/1760.
- `Render_Dirty.cc` background save/restore: the world background lives in a world-sized save
  buffer, and the UI save buffer stays UI-sized.
- `Isometric_Utils.cc`: `GetMouseXY`/`GetMouseWorldCoords`/`GetMouseMapPos` convert
  `gusMouseXPos` (UI space) through `UiToWorld()`. `SetSafeMousePosition`, edge-scroll
  detection (`RenderWorld.cc:2015`) and the tactical viewport mouse region use the new mapping.
- The viewport covers the **whole window** (the world renders under the bottom HUD). This is
  cheap once the UI is a separate layer and gives more visible map on widescreen.

**4c. Things drawn "in the world" by UI code** (decide per item)
| Item | Where | Layer |
|---|---|---|
| Soldier names, HP/AP bars, "hit" damage numbers | `Interface.cc` `RenderTopmostTacticalInterface`, `Soldier_*` | UI layer, positioned via `WorldToUi()` (stays readable at any zoom) |
| Tile cursors, path dots, AP cost at cursor | `Interface_Cursors.cc`, `PathAI` rendering | World layer (they're tiles) + UI for the text |
| Speech bubbles / talking faces / locators | `Faces.cc`, `Interface_Dialogue.cc` | UI layer |
| Bullets, explosions, smoke, lights | `RenderWorld`, `Tile_Animation.cc` | World layer |
| Video overlays (`ExecuteVideoOverlays`) | `Render_Dirty.cc` | Per overlay: flag `UI`/`WORLD` |

**4d. Overhead map and editor.** The overhead map (`Overhead_Map.cc`) renders into the world
layer at its own scale. The editor (`src/game/Editor/`, `EDITOR_TASKBAR_*`) is UI-layer
taskbar plus world layer. Keep editor support, even if at first it is locked to `Zw == Su`.

**4e. Fallback mode.** Add a config `"world_zoom": "match_ui"`. It keeps the Phase 1 behaviour
(single layer), which is the safe default until the split is proven. Headless always runs as
a single layer at `Zw = Su = 1`.

**Done when:** at 2560x1440 with `Su = 2` and `Zw = 1`, the HUD looks identical to 1280x720 and
the world shows the 2560x1440 view. Clicking tiles, edge scrolling, targeting, the overhead map
and dialogue all work, `tactical_move_merc.lua` passes at mixed scales, and frame time at 4K is
≤ 1.2x that of Phase 1.

---

### Phase 5 — Runtime video settings

- An options-screen **Video** tab (`Options_Screen.cc`) with resolution, window mode, UI scale,
  world zoom (tactical `+`/`-` hotkeys too), filter and FPS cap.
- Apply without restart:
  1. Tear down and recreate the SDL textures and software surfaces (`Video.cc`,
     `SysUtil.cc` for `guiSAVEBUFFER`/`guiEXTRABUFFER`, the Z-buffer in `GameScreen.cc`).
  2. `g_ui.setScreenSize()` + `recalculatePositions()` (idempotent since Phase 2).
  3. Re-enter the current screen: most screens build their regions in `Enter*`/`Exit*`, so
     force an exit and re-enter of the current screen and a full `InvalidateScreen()`.
     Screens that cache positions in statics are a known risk; audit statics initialised from
     `SCREEN_WIDTH` or `STD_SCREEN_*`.
- Handle `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` and display changes (drag to another monitor)
  by recomputing the logical size when the UI scale is `Auto`.
- Persist settings to `ja2.json` through the Rust config writer.

**Done when:** changing resolution or scale on the tactical screen, map screen and laptop has
no crash and no stale regions (region audit passes after each change). An e2e test
toggles through three modes.

---

### Phase 6 — Follow-ups (separate initiatives, not scheduled)

- **HD fonts:** load TTF through SDL_ttf into the UI layer at physical resolution, so text is
  sharp at non-integer scales. Needs a glyph-cache version of `Font.cc` and the text registry.
- **HD UI asset overrides:** a VFS convention such as `interface/foo@2x.sti`/`.png`, picked when
  `Su ≥ 2`. The layered VFS (`build/externalized` overrides `.slf`) already fits this.
- **32-bit colour pipeline:** RGB565 → ARGB8888 in blitters and shading tables. Improves
  gradients and lighting but touches all 42 blitters.
- **GPU world renderer:** tiles and sprites as textures. Needed for smooth fractional world zoom.
- **Post-processing:** optional CRT/scanline or colour grading shaders in the compositor.

---

## Cross-cutting concerns

- **Headless and automation.** Screenshots (`ja2ctl shot`) must capture the *composited*
  frame. Add a CPU composite path used when headless and by the screenshot command. Automation
  click coordinates stay in UI space, and `ja2ctl ui` output keeps UI coordinates.
- **Mods and externalized data.** Any positions in `src/externalized/` JSON (e.g. game policy,
  UI tweaks) keep their 640x480 meaning and are offset through anchors. Anchors are not
  exposed to JSON until Phase 3 has settled.
- **Save games.** No change. Nothing in this plan is serialised, except that the options
  already stored in the game settings file stay compatible.
- **Performance.** At 4K with `Zw = 1` the software world renderer fills 8.3 Mpx per full
  redraw, which is 27x the 640x480 cost. Profile `RenderStaticWorldRect` and the Z-buffer
  clears early in Phase 4. Mitigations: keep dirty-rect rendering, SIMD the hot blitters, cap
  `Zw` at 1 only for displays ≤ 1440p by default.
- **Platform.** Test on Windows (MSYS2), macOS (Retina = HiDPI path) and Android
  (`android/`: fixed fullscreen, touch mapping uses the same UI↔world transform).

## Main risks

| Risk | Mitigation |
|---|---|
| Hundreds of hand-tuned offsets break silently | Phase 0 golden images + region audit, screen-by-screen PRs |
| UI code that relies on reading world pixels (shadows, translucency over world) | Found by grepping `ShadowVideoSurfaceRect`, `Blt*Translucent`, `guiSAVEBUFFER` reads in UI code; convert to alpha on the UI layer |
| Static caches of screen positions break runtime resize | Phase 5 audit; worst case the resolution change requires going to the main menu |
| Software world rendering too slow at 4K 1x | Default `Zw` to `match_ui` on 4K; profile before committing to defaults |
| Merge conflicts with upstream Stracciatella | Keep changes behind `UILayout` helpers; upstream-able PRs per phase |

## Suggested first PRs

1. Phase 0: resolution matrix + region-audit assertion.
2. Phase 1: `ui_scale`/`"auto"` resolution config through Rust → C API → `SGP.cc` →
   `Video.cc`, with unit tests for the logical-size function.
3. Phase 1: sharp-bilinear presentation replacing the 4x oversample.
4. Phase 2: `UILayout::anchor()` + the tactical bottom bar fillers.
