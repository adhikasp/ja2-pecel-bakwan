# Native modern game — Phase 0 decisions

Results of the Phase 0 spikes of [native-modern-game.md](native-modern-game.md). Measured on the dev machine
(AMD Ryzen 5560U, Radeon integrated graphics, Windows 11, MSYS2 MinGW64, RelWithDebInfo `-O2`).
Reproduce with the commands in each section.

## Summary

| Question | Decision |
|---|---|
| UI toolkit | **RmlUi** (6.3, MIT), with our own SDL render interface and data models as the view-model layer |
| Render path | **SDL_Renderer on its GPU drivers now; SDL_GPU (own pipelines) for the world renderer in Phase 8.** Headless screenshots: GPU render target + readback where a GPU exists, software renderer otherwise |
| World renderer | Instances (sprite, shade LUT, depth, depth-test op) in the legacy submission order against a depth buffer reproduce the software renderer **pixel for pixel (0.0000 % diff)**; build it on SDL_GPU with a depth texture |
| Assets | Tooling in `tools/assets/` (stdlib Python); 7,615 assets classified; derived art only in `~/.ja2-assets` |

## 1. UI toolkit: RmlUi vs in-house

The same save/load screen (40-row scrolling list, header, Load/Delete/Close buttons with shortcut hints, a
confirmation modal, a tooltip following the mouse) in both, sharing the view model (`src/spike/UiSpike.h`), the
design tokens and the font (Lato). In game: `ja2.debug("uispike_rml")` / `ja2.debug("uispike_inhouse")`;
headless without the game: `ja2-spike ui rml 1920x1080 out.png hover`. Test: `tests/spike/ui_spike.lua`.

### Performance (ms per frame, layout + draw)

GPU path (`ja2-spike renderpath`, offscreen render target, synced with a 1-pixel readback):

| Renderer | 1080p RmlUi | 1080p in-house | 4K RmlUi | 4K in-house |
|---|---|---|---|---|
| SDL_Renderer / direct3d11 | 1.49 | 1.63 | 3.11 | 4.16 |
| SDL_Renderer / direct3d12 | 1.29 | 1.59 | 2.85 | 3.70 |
| SDL_Renderer / gpu (SDL_GPU) | 1.16 | 1.97 | 2.62 | 3.81 |
| SDL_Renderer / opengl | 0.86 | 1.90 | 2.03 | 3.17 |

Software path (SDL software renderer, what headless screenshots use; `ja2-spike perf`):

| | 1080p | 1440p | 4K |
|---|---|---|---|
| RmlUi | 25.9 | 44.7 | 109.3 |
| in-house | 18.1 | 32.4 | 72.0 |

In the game (software, plus the ARGB→RGB565 copy into the frame buffer): RmlUi 29.0 / in-house 21.1 ms at
1080p, 120.2 / 83.3 ms at 4K.

The whole frame at 4K is within the 2 ms budget only on the GPU at 1080p; at 4K both are ~3 ms including the
readback sync. RmlUi is the *faster* of the two on the GPU (it batches glyphs into atlas geometry; the naive
in-house layer draws one texture per glyph). Neither is usable on the software path at 4K for interactive play,
which is fine: the software path is only for headless screenshots.

### Comparison

| | RmlUi | In-house |
|---|---|---|
| Code for the spike screen | 110 lines RML/RCSS + ~300 lines C++ (render/system interfaces, bindings) | ~560 lines C++ (layout, widgets, text, drawing) |
| Layout | flexbox, box model, `dp` units, overflow/scroll, absolute overlays | a 50-line flex subset; everything else hand-placed |
| Text | FreeType, wrapping, kerning, font fallback, glyph atlases | FreeType, no kerning, word wrap had to be written for the modal |
| Binding | data models: `{{ }}`, `data-for`, `data-class-*`, `data-event-*` → C++ callbacks | direct calls; every redraw re-reads the model |
| Automation | element ids for free (`ja2.spike().elements`) | ids only where the code records them |
| Moddability | RML/RCSS are data: overrides on disk win (`JA2_SPIKE_ASSETS`) | C++ only |
| Headless | works through any SDL_Renderer, incl. the software one | same |
| Look | identical tokens; RmlUi's rounded borders show hairline seams on SDL's software rasteriser (not on GPU) | pixel-snapped rects, clean on both |
| Build | core compiled from the fetched source into a static lib (~1–2 min); FreeType from the system or fetched | FreeType only |
| Platforms | Windows (built here), Linux/macOS (CI), Android: C++17 + FreeType, no platform code | same |
| Licence | MIT (RmlUi), FreeType FTL/GPLv2, Lato SIL OFL | — |

Gotchas found: `data-model` on `<body>` silently breaks `data-for` (use a wrapper div); RmlUi has no user-agent
stylesheet (`div`, `p`, `h1` are inline until styled); no `%` operator in data expressions; member getters must
be non-const; RmlUi's own CMake installs itself into our packages, so we compile its core sources ourselves;
MinGW needs `-include cstdint` for its `robin_hood.h`.

**Decision: RmlUi.** Layout, text and binding would take months to reach RmlUi's level in-house, RmlUi is faster
on the GPU, and RML/RCSS gives moddable UI. The view models stay toolkit-free C++ (`SaveListModel`), which keeps
the choice reversible.

## 2. Render path: SDL_Renderer vs SDL_GPU

`ja2-spike renderpath` (20,000 32x32 sprites from a 1024² atlas; full readback = headless screenshot cost):

| Backend | 1080p batch | 1080p 1 call/sprite | 4K batch | 4K readback |
|---|---|---|---|---|
| software | 347 | 347 | 371 | 8.6 |
| direct3d11 | 7.4 | 8.3 | 7.0 | 17.4 |
| direct3d12 | 5.9 | 7.3 | 6.2 | 15.4 |
| gpu (SDL_GPU) | 6.7 | 8.0 | 6.0 | 18.2 |
| opengl | 5.5 | 7.5 | 6.3 | 16.1 |
| vulkan | 6.4 | 8.6 | 6.8 | 254.5 |
| raw SDL_GPU, windowless (d3d12), `SDL_BlitGPUTexture` per sprite | – | 104 | – | 1.7 |

- A GPU device works without a window (`SDL_CreateGPUDevice` + render target + download): headless GPU
  screenshots are possible; the Vulkan readback path is slow here (254 ms at 4K), D3D12/SDL_GPU is 1.7 ms raw.
- SDL_Renderer has no depth buffer and no custom shaders, and per-draw blits on raw SDL_GPU are slow; the world
  needs instanced quads + depth + palette/shade lookup in a shader.

**Decision:** UI through RmlUi on SDL_Renderer (its `gpu` driver, i.e. SDL_GPU underneath) now; the world renderer
(Phase 8) directly on SDL_GPU with our own pipelines (shaders via SDL_shadercross at build time), sharing the
device with the UI compositor. Headless: keep the software path for CI/screenshot determinism; GPU offscreen
capture is available where a GPU exists.

## 3. World renderer

`ja2.spikeWorld(dir)` (`src/game/TileEngine/WorldSpike.inl`, raster in `src/spike/WorldSpikeRaster.cc`), test
`tests/spike/world_spike.lua` (Omerta A9 after the landing, then scrolled).

- Every static node of the land, object, shadow, structure, roof, on-roof and topmost layers becomes an instance:
  decoded ETRLE frame (the "atlas"), the vobject's shade table for its light level (a palette LUT), screen
  position, depth, and one of five ops mapped from the legacy blitters: plain (land), depth `>=` (BltTransZ),
  depth `>` per column (multi-Z wall strips: `ZStripInfo` becomes a per-column depth ramp), shadow darken with
  depth `>`, pixelated obscured.
- Instances are drawn in the legacy submission order against a depth buffer — what a GPU does with a depth test.
- **Result: 0.0000 % of 1,843,200 pixels differ** (1920x960 viewport, 13,929 instances from 877 frames), and 0 %
  after scrolling (14,206 instances). CPU timings: legacy 7.5 ms, instance build 4.5 ms, raster 16 ms.
- Found on the way: the first diff was 0.72 % — the legacy static pass skips the topmost layer (roof wireframes)
  unless a full redraw reset the layer-optimisation flags. The reference now does what a full redraw does.
- Not covered yet: items (outline blitter), rotting corpses/vehicles (multi-trans-shadow), animated and dynamic
  tiles, mercs, lighting changes, translucency. Phase 8 adds them with the same equivalence test.

## 4. Asset tooling

`tools/assets/` (standard library Python, tests in `tools/assets/tests`, ctest `spike_asset_tools`):

- `extract.py`: all 7,602 STI/PCX images of JA2 Gold (11 archives + loose files) → PNG + `meta.json`
  (frames, offsets, palette, flags, app data / AuxObjectData) in **286 s, 0 failures**, to `~/.ja2-assets`.
  SLF layout verified against the code: entry count int32 at offset 512, 280-byte entries at the end.
- Usage map: `CreateImage` hook (`SetImageLoadHook`) + `ja2.recordImageUsage()`; tour
  `tests/spike/asset_usage_tour.lua` records 300 images on 9 screens; `usage.py` aggregates.
- `manifest.py` → `assets/manifest.json` (7,615 assets: upscale 6,577, design-new 584, regenerate 331,
  repaint 96, replace 27) + HTML inventory report; rules in `ja2assets/classify.py`.
- `gates.py`: exact scale multiple, silhouette IoU, colour drift, frame stability, tile seams, size budget, and a
  comparison sheet; `baseline.py` makes nearest-neighbour candidates (they pass all gates).

## CI

`ctest -L spike` (added to `.ci/ci-build.sh`): UI toolkit unit tests (both toolkits, all states, input by id),
`ja2-spike selftest`, render path (software), world raster rules, asset tool tests. `ctest -L spike-gamedata`
runs the in-game spikes (needs the game data).

# Phase 2 — native UI runtime

What [native-modern-game.md](native-modern-game.md) Phase 2 built, and the decisions taken on the way.

| Question | Decision |
|---|---|
| Where the runtime lives | `src/nativeui/` (toolkit layer, no game code: render interface, clock, tokens, fonts, icons; shared with the spikes and unit tests) and `src/game/NativeUI/` (the game side: `NativeUI.h`). `WITH_NATIVE_UI` (default on, off on Android) builds it; without it a stub keeps every screen legacy |
| Render paths | **One SDL_GPU compositor and present path** (`src/sgp/VideoGpu.cc`, the Phase 2 follow-up issue #50). In a window the game builds an SDL_GPU device, claims the window, and draws everything — the world layer (the Phase 8 texture, or the software WORLD_BUFFER uploaded), the legacy 8-bit `ScreenBuffer` and the native UI — as triangles of one shared graphics pipeline (`src/sgp/shaders/quad.vert`/`quad.frag`), then presents with `SDL_SubmitGPUCommandBuffer`. There is no `SDL_Renderer` on this path. The world's compute-written texture is copied into a sample-only texture first, which is what makes it sampleable on D3D12 (without it, D3D12 returned black while Vulkan was fine). The native UI's triangles come from `src/nativeui/UiGpu.cc` (`nui::SdlGpuRenderInterface`, RmlUi's own premultiplied colours); the UI records and uploads before the pass (`VideoOverlay::GpuFramePrepare`) and draws inside it (`GpuFrameRender`). **Software:** headless and every automation session (or `JA2_NATIVE_UI_RENDERER=software`) rasterize the native layer into a premultiplied ARGB layer that is blended into the ScreenBuffer, so screenshots, pixel reads and goldens include it; there the game keeps the `SDL_Renderer` path. `JA2_NATIVE_UI_RENDERER=gpu` forces the GPU UI in a shown automation session |
| Size and scale | output pixels (window pixels on the GPU path, the ScreenBuffer headless); `1dp = min(w / 1920, h / 1080) × native_ui_scale`, clamped so the layout is never smaller than 1280x720 dp. Resizes, video-mode changes and DPI changes are picked up every frame |
| Minimum output | 1280x720 pixels. Below that the native UI does not run and every screen resolves to legacy (so 640x480 keeps the legacy screens) |
| ui_mode | `ja2.json` `"ui_mode": { "credits": "native", "msgbox": "legacy", ... }` and `"native_ui_scale": 1.25`; runtime override `ja2.setUiMode(key, mode)`. Resolved once when a screen is entered (`NativeUI::HandleScreen` in the game loop). Keys: `credits` (default native), `msgbox`, `tooltip`, `toasts`, `cursor` (default legacy) |
| Input | a native screen or a native modal takes the mouse (legacy regions get nothing) and gets the keyboard queue first; RmlUi's own navigation (Tab, arrows via `nav`, Enter/Space on the focused element) with `:focus-visible` rings; text fields start SDL text input |
| View models | `ViewModel.h`: fields declared once (`Describe`), bound to RmlUi by `Binding`, snapshotted for tests and `ja2.viewModel`; commands are named callbacks; `NativeUI::Notify(TOPIC_...)` is called where the game changes money, time, sector and team |
| Strings | new UI strings are in `assets/ui/strings/strings-<lang>.json` (English fills gaps). The translations of the Phase 2 strings need a translator's review |

## Obsolete pixel-coordinate UI settings

The plan asks which moddable settings stop meaning anything once screens are native (they place things in the 640x480
legacy layouts). None is removed yet: each stays until the screen that reads it has gone native, then it is dropped
with that screen's legacy code (Phase 10).

| Setting | Where | Used by | Obsolete when |
|---|---|---|---|
| `townPoint` (`x`, `y`) | `strategic-map-towns.json` | town name labels on the 640x480 strategic map image | Phase 4 (native map places labels from sector data) |
| `squad_size` pixel rule ("640 + (n-6)*83 px") | `game.json` (comment and the check in the tactical panel) | the legacy squad bar's fixed-width slots | Phase 5 (the native squad bar wraps/scrolls; `squad_size` itself stays) |
| Credits record codes `D`, `B`, `S`, `J`, `C`, `R` (spacing, speed, justification, colours) | `credits` EDT (game data, moddable) | legacy credits | **now**, on the native credits screen (docs/ui/credits.md §8); still read by the legacy screen |
| Credit face rectangles (`gCreditFaces`) | code, not data | legacy credits hit regions | now (native cards); kept as the crop rectangles for the portraits |
| `eyesXY`, `mouthXY` | `mercs-rpc-small-faces.json` | face animation offsets | **not** obsolete: they are relative to the face art, not to a screen layout |

Nothing else in `assets/externalized/` holds screen coordinates: the other UI positions are compiled into the legacy
screen code (`STD_SCREEN_*`, `UILayout`), which Phase 10 deletes.

# Phase 3 — front-end screens

What [native-modern-game.md](native-modern-game.md) Phase 3 built. The per-screen parity contracts are
[mainmenu.md](../ui/mainmenu.md), [options.md](../ui/options.md), [saveload.md](../ui/saveload.md),
[newgame.md](../ui/newgame.md) and [loadingscreen.md](../ui/loadingscreen.md); the owner approved each screen's wireframe
(`native-phase-3-wireframes/` on the `pr-screenshots` branch).

| Question | Decision |
|---|---|
| Defaults | `mainmenu`, `options`, `saveload`, `newgame` and `loadscreen` are native by default; below a 1280x720 output every one of them is legacy. A video change re-decides the current screen (`NativeUI::ScreenRelaidOut`), so a 640x480 legacy menu becomes native at 1920x1080 and back |
| Shared logic | the native screens call the legacy screens' game code, not copies of it: `SaveLoadScreen.cc` exports `SaveLoadNative*` (list, save, delete, leave target, compatibility), the main menu its splash (`HandleMainMenuSplash`); the options screen calls the same setters, `SaveGameSettings` and the world refreshes the legacy exit does |
| Loading a save | the native save/load screen asks its questions natively, then hands over to the legacy screen's "load upon entry" path (`Screen::Finished`), which loads with the game's own fades and error handling. Continue on the main menu uses the same path |
| Loading screen | loading blocks the game loop: `DisplayLoadScreenWithID` and the progress bar call `NativeUI::ShowLoadingScreen/LoadingProgress/LoadingStep`, which draw and present synchronously; the screen closes at the next frame |
| Video settings | the options screen's Video page replaces the legacy Video screen (`OPTIONS_SCREEN` accepts video changes when the native options screen runs) |
| Save thumbnails | a 480 px PNG next to each save (`<save>.png`), written by `SaveGame` from a snapshot of the map/tactical picture (`NativeUI::SnapshotGameFrame`); nothing in the save file changes; saves without one show the sector's load screen art |
| Full-screen art | the main menu art and load screens come from the player's data at runtime; the menu art is uplifted 4x with Scale2x and filtered to the screen size (cover: aspect kept, cropped). Nothing derived is written anywhere |
| New settings | `reduced_motion` (native UI: no autoscrolling credits) and `native_ui_scale` are written to ja2.json from the options screen (`ContentManager::saveNativeUiSettings`) |
| Keys on release | Esc and Enter act on key release on the native options, new-game and save/load screens, so a release never answers the next message box or quits the main menu (which acts on release, as legacy) |
| Automation | `ja2.viewModel(name)` prefers an open screen's view model to one it made before, and makes a new one each time otherwise (`options`, `mainmenu`, `saveload` read the game when made); `ja2.state().messageBoxText` reads native message boxes; `ja2.debug("mock", "phase3/<name>")` shows a wireframe through the runtime |
| Strings | new strings are English in `assets/ui/strings/strings-eng.json`; the other languages fall back to English and carry a `_todo.phase3` note for a translator |

# Phase 8 — native world renderer

What [native-modern-game.md](native-modern-game.md) Phase 8 built, measured on the same dev machine (Ryzen 5560U,
Radeon Vega 7 integrated, Windows 11, Vulkan through SDL_GPU 3.4).

| Question | Decision |
|---|---|
| Scene | **The legacy traversal records, the GPU rasterizes.** `RenderTiles` stays the one scene walk (which nodes, which blitter, positions, heights, depths, shade levels, glow, items, corpses, mercs, animated tiles, reveal translucency). With a recorder set it does not blit: each blitter call becomes an instance naming that blitter (`WorldPipe::Op`, 23 of them) with its clipped rectangle, palette (the shade/lighting table it would use), depth and outline colour. The Phase 0 spike re-derived the traversal and covered static tiles only; recording covers everything the software renderer draws, by construction |
| Raster | **A compute shader, not the raster pipeline.** One thread per world pixel walks the instances covering its 16x16 bin in submission order (the workgroup loads them into shared memory 256 at a time), with colour (RGB565) and depth in registers. That makes the blitters that read the destination (shade-table shadows, 50 % translucency, shadows over shadows) exact, which fixed-function blending and a depth texture cannot be. One GLSL source, `src/sgp/shaders/world_raster.comp` |
| Same rules twice | `WorldPipe::ApplyPixel` (C++) is the reference; the shader is its transcription. `world_renderer = pipeline` runs the recording on the CPU implementation, so CI checks the recording, order and shading logic without a GPU |
| Setting | `ja2.json` `"world_renderer": "software" \| "gpu" \| "pipeline"`, `JA2_WORLD_RENDERER` wins, `ja2.setWorldRenderer()` at run time. gpu/pipeline make the world a layer of its own (at the UI scale if no world zoom is set). gpu creates the SDL renderer on an SDL_GPU device of the platform's default driver (D3D12 on Windows, Vulkan, Metal; `JA2_GPU_DRIVER` picks another) and shares it; the world texture is wrapped as an `SDL_Texture` and drawn as the world layer (`SDL_SCALEMODE_PIXELART`). Headless and driven sessions read the result back into the WORLD_BUFFER |
| Default | **gpu in real windows**, software in headless and driven sessions (their screenshots are the references). Automatic fallback to software when no device or pipeline can be created, or a GPU frame fails |
| Backends | `tools/shaders/build.sh` builds, from the GLSL source: **SPIR-V** (glslangValidator), **DXBC SM 5.1** for D3D12 (spirv-cross to HLSL, then D3DCompile from Windows' own `d3dcompiler_47.dll`, no download), **MSL** (spirv-cross, from a variant with Metal's flat slot numbers). All three are committed in `world_raster.comp.h`; the renderer picks what the device takes. Verified on this machine: D3D12 (default) and Vulkan, both headless and in windows, 0 px on every scene. **MSL is generated but not verified: no Mac here** |
| Static cache | The static passes (land, objects, shadows, structures, roofs, on-roof, topmost and the obscured passes) are recorded once and reused while their inputs stay the same: camera, viewport and world size, the render/redundancy/roof/item flags, the shade table, the sprite pool, and no RENDER_FLAG_FULL/MARKED (when the software renderer would redraw the static world too). Item outlines in the cache follow the glow cycle each frame. The dynamic layers are recorded every frame |
| Zoom | **Fractional world zoom in 1/8 steps (1.0–4.0)** with the gpu renderer: the layered layout takes the zoom in eighths (`LayerLayout::worldZoomQ`, `ComputeLayerLayoutFine`), `UiToWorldQ`/`WorldToUiQ` map exactly (integer math), so picking follows. Keypad +/- and Ctrl+wheel step it. Software and pipeline keep whole zooms. Only the whole part is saved to ja2.json |

## Equivalence (1x, before any HD art)

`tests/e2e/world_renderer.lua` (`ctest -R e2e_world_renderer`), Omerta after the landing, 1920x1080 world viewport
(2,073,600 px compared per scene), software full redraw vs the recorded instances:

| Scene | Instances | CPU pipeline | GPU (D3D12 and Vulkan) |
|---|---|---|---|
| day, Barry | 10,826 | 0 px (0.0000 %) | 0 px (0.0000 %) |
| items (outlines/glow) + 2 corpses | 12,387 | 0 px | 0 px |
| interior (roof taken off) | 12,363 | 0 px | 0 px |
| night, lights on (96 shade palettes) | 11,993 | 0 px | 0 px |
| scrolled to the map edge, night | 9,555 | 0 px | 0 px |
| world zoom 1.5 (1280x720 world) | 4,967 | 0 px | 0 px |
| night at world zoom 2, 4K window | — | — | 0 px (D3D12) |

Also checked: the composed frames (UI over world, static cache in use) match software at all but a few sampled
pixels (animations move on between the screenshots); tiles aimed at by their centre are picked right at zoom 1 and
1.5; and **picking**: at 144 screen points (headless) the cursor tile, the interactive tile (doors etc., found during the recorded
topmost pass) and the soldier under the mouse are the same as with software. `WorldPipeline_unittest` checks every
non-strip op against its legacy blitter (clipped and unclipped, random sprites, colours and depths).

What is verified where: CI without a GPU runs the unit test, `e2e_world_renderer` (pipeline always; GPU part skipped
and reported) and `e2e_tactical_layers_pipeline` (the whole tactical test with the pipeline drawing every frame).
The GPU numbers above are from this machine, headless (windowless Vulkan device) and in a window.

Found on the way (legacy quirks the recording reproduces):
- `BltTransZTransShadowInc` (corpses, multi-tile mercs) steps its depth by `Z_SUBLAYERS` over opaque pixels but by
  `Z_STRIP_DELTA_Y` over transparent runs, so its depth depends on the row: those instances carry a depth per pixel.
  Its obscured twin starts the checkerboard from the unclipped top row. The left-clip start of both steps by
  `Z_SUBLAYERS` and reads past the 16 strip changes into the struct.
- Some blitters differ between their clipped and unclipped versions (`<` vs `<=` in the obscured outline blitter);
  they are separate ops.
- A newly added corpse is `LASTDYNAMIC`: the static pass clears the flag and draws it with Z write, so a recording
  without side effects must clear and restore it or the corpse is drawn twice.
- `ColorFillVideoSurfaceArea` is clipped to the clipping rectangle, so it does not clear off-map areas.

## Performance

Real 3840x2160 window, **wall clock** (`tests/perf/world_renderer_perf.lua`: a driven session switches to the wall
clock with `ja2.realClock{readback=false, fps=0}` after loading, so nothing is read back and the frame rate is not
capped; intervals are measured between presents, 5 s per case). "uncached" re-records every static tile each
frame, which is what a scrolling frame costs.

| | Driver | GPU cached (still) | GPU uncached (scrolling) | Software (incremental, still) |
|---|---|---|---|---|
| 4K, world zoom 1 (3840x2160 world, 37,431 instances) | D3D12 | 12.1 ms, 82 fps (p95 13.2) | 17.5 ms, 57 fps (p95 19.1) | 6.3 ms |
| | Vulkan | 13.9 ms, 72 fps (p95 15.3) | 17.6 ms, 57 fps (p95 19.1) | 7.9 ms |
| 4K, world zoom 2 (1920x1080 world, 10,596 instances) | D3D12 | 6.4 ms, 156 fps | 6.8 ms, 148 fps | 4.5 ms |
| | Vulkan | 8.5 ms, 117 fps | 8.6 ms, 117 fps | 6.3 ms |

Recording costs 3.3 ms per frame with the cache (dynamic layers only) and 13 ms without it at 4K zoom 1, down from
14–16 ms before the cache, a per-frame sprite check and a palette cache. **4K at 60 fps holds standing still and
at zoom 2 always; scrolling at 4K zoom 1 runs at 57 fps.** The software renderer's still frames are cheap because
it redraws incrementally; it does not scale or zoom fractionally.

## Gaps

- Scrolling at 4K world zoom 1 misses 60 fps by a little (57). Next step: keep the static cache across camera
  moves by recording a margin around the view and re-recording only strips, as the software renderer does.
- The camera still moves in the game's own steps. Zoom is fractional (1/8 steps), but there is no free sub-pixel
  camera or zoom animation between steps; each step rebuilds the world layer.
- MSL is generated but not compiled or run (no Mac).
- The screenshots of driven windows are composed from the read-back world buffer with nearest scaling; the player
  sees the GPU texture scaled with SDL's pixel-art filter.
- Night vision has no separate render path in the game (lighting is the shade palettes, covered). Explosions are
  anitiles and go through the recording; there is no live-explosion scene in the equivalence test.
