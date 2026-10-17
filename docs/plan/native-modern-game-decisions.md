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
| Default | **gpu in real windows**, software in headless and driven sessions (their screenshots are the references; the 888 pipeline expands to the same pixels for the shipped art, and the CPU pipeline is several times slower per frame). Automatic fallback to software when no device or pipeline can be created, or a GPU frame fails. The pipeline is exercised headless by `e2e_world_renderer`, `e2e_tactical_layers_pipeline` and `e2e_world_renderer_light` |
| Backends | `tools/shaders/build.sh` builds, from the GLSL source: **SPIR-V** (glslangValidator), **DXBC SM 5.1** for D3D12 (spirv-cross to HLSL, then D3DCompile from Windows' own `d3dcompiler_47.dll`, no download), **MSL** (spirv-cross, from a variant with Metal's flat slot numbers). All three are committed in `world_raster.comp.h`; the renderer picks what the device takes. Verified on this machine: D3D12 (default) and Vulkan, both headless and in windows, 0 px on every scene. **MSL is generated but not verified: no Mac here** |
| Static cache | The static passes (land, objects, shadows, structures, roofs, on-roof, topmost and the obscured passes) are recorded once and reused while their inputs stay the same: camera, viewport and world size, the render/redundancy/roof/item flags, the shade table, the sprite pool, and no RENDER_FLAG_FULL/MARKED (when the software renderer would redraw the static world too). Item outlines in the cache follow the glow cycle each frame. The dynamic layers are recorded every frame |
| Zoom | **Fractional world zoom in 1/8 steps (1.0–4.0)** with the gpu renderer: the layered layout takes the zoom in eighths (`LayerLayout::worldZoomQ`, `ComputeLayerLayoutFine`), `UiToWorldQ`/`WorldToUiQ` map exactly (integer math), so picking follows. Keypad +/- and Ctrl+wheel step it. Software and pipeline keep whole zooms. Only the whole part is saved to ja2.json |
| Colour | **8 bits per channel (0xRRGGBB), not RGB565.** A frame's palettes are the art's 24-bit colours where the object has them (`SGPVObject::CurrentShade24`, the 24-bit palette shaded exactly like the legacy table); entities whose shade table the game built separately (mercs, corpses) keep their 565 table, expanded. The shader writes RGBA8 and the compositor samples the 888 world layer (`VideoSetWorld888`). **For the shipped art the palette is already RGB565-quantised, so the picture is pixel-identical to the software renderer's expanded output (0 px, re-verified)**: the 32-bit path pays off with HD art and with the lighting gradients, not with the current palette |
| Lighting | **Applied in the pipeline (CPU reference and shader), not only baked into the shade tables.** A frame carries an ambient + sun and a list of point lights (`WorldPipe::Lighting`); the pass runs once per pixel after every op, so the destination-reading ops stay exact. The game feeds it its own light sprites (`Lighting.cc`: muzzle flashes, explosions, lamps) through `CollectLighting`; `ja2.setWorldLights` adds more (a driving hook). `e2e_world_renderer_light` checks it on the CPU and, where there is a device, the GPU. **Note:** the game's own lights are also baked into the tile shade tables (`LightDraw`), so a lamp or an explosion is lit twice — by the baked shade and by the shader. That double count is a transitional state until the sector's light map moves into the shader (a follow-up); the same reason there is no global ambient yet |

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

### Re-measured on a slower box (#37): what is left of that

The table above was taken on the machine PR #19 used. On a second box (Windows/MSYS2, AMD Radeon iGPU, D3D12,
3840x2160 @ 59 Hz) the same harness with `#37`'s strip cache and its GPU frame-identity check measures:

| 4K, world zoom 1 | mean (fps) | median | p95 | record | static rows |
|---|---|---|---|---|---|
| gpu, still, cache on | 78.9 ms (13) | 96.7 | 102.1 | 7.4 ms | 102 recorded / 380 reused |
| gpu, still, cache off | 77.4 ms (13) | 99.0 | 103.0 | 10.6 ms | all re-recorded |
| gpu, **scrolling**, cache on | 48.6 ms (21) | **17.1** | 99.1 | 7.8 ms | 102 / 380 |
| gpu, scrolling, cache off | 102.0 ms (10) | 101.1 | 108.7 | 10.9 ms | all re-recorded |
| software, still | 47.1 ms (21) | 17.2 | 99.1 | 0 | — |

The baseline on that box (before the strip cache) was 85.4/86.8 ms (12 fps) standing still with a hot cache and
87.5/81.0 ms re-recording, with 10.5–10.8 ms of it record. So:

- **The GPU dispatch dominates, not the record.** The CPU half lands: record drops to ~7.4–7.8 ms at 4K in a real
  window (10.5 before), and in the driven path the equivalence scenes' scrolled frame records in 1.1–1.4 ms at
  1920x1080 (against 3–5 ms re-recording) and 4.4 ms at 4K (against 8.7).
- **The GPU half only lands where consecutive frames are identical.** In the scrolling case more than half the
  frames are the one already in the target and cost 17 ms (the 60 fps budget) instead of 101 ms: the median is
  17 ms and the mean over the case halves (102 → 49 ms). Standing still the item glow and idle merc animations
  change the frame every frame, so the floor stays ~79 ms.
- **The exposed-region dispatch is what is left.** Dispatching only what a camera move exposes needs the shader
  to take a dispatch origin and the previous frame as input (a texture-to-texture shift, so the translated part
  is not recomputed at all). That is a shader change, and it is the GPU half that still has to be done.

### The dispatch origin, and what the 4K floor actually is (issue #37, second half)

The shader now takes a **dispatch origin**, and `WorldGpu::Renderer` uses it to dispatch only the region that
changed since the last frame: the bounding box of the instances whose own bytes, resolved palette colours or
depth runs differ (a readback still dispatches the whole target, so the equivalence path still sees every
pixel). A frame that changed nothing is not dispatched at all. Standing still the region is typically 32x64 to
48x64 pixels of a 3840x2160 target, against the whole target before.

The measurement on this machine (Ryzen 5 5560U, AMD Radeon iGPU, D3D12, 3840x2160 @ 59 Hz, display at 300 %
scaling) says the region does not move the frame interval, because **the dispatch is not what limits a 4K
frame here**:

- Through the driven read-back path (no window present involved), a full 4K dispatch of 32,171 instances plus
  the read-back is **17-19 ms**. The dispatch itself is a few ms of that.
- In a real window the GPU frame interval is **~99 ms** (median), while the software renderer's is **~17 ms**
  (median). A frame the GPU renderer *skipped entirely* - an identical frame, nothing bin/uploaded/dispatched -
  is **also ~99 ms**. The interval is therefore independent of the world dispatch: presenter/compositor, not
  `RenderWorld`. The arithmetic alone settles it: the software renderer's whole 4K frame is 17 ms, so even a
  world dispatch on *every* frame would put the GPU path at ~35 ms, nowhere near 99 ms.
- The harness's own numbers, before and after the change, are the same within noise: GPU cached still 86.5 to
  86.4 ms mean (96.8 to 98.9 median), GPU uncached still 76.3 to 74.4 ms mean (99.2 to 99.1 median), GPU cached
  scrolling 48.0 to 53.0 ms mean (17.3 to 17.1 median). At 1920x1080 every GPU case is at the 60 fps vsync cap
  both before and after, so there is no headroom to measure either.

So the dispatch-origin half is landed, exact (the six scroll scenes are still 0 px) and cheap, but the 4K
floor it was meant to move is not where the issue's plan put it. The texture-to-texture **frame shift is not
done**: a camera move that changed nothing but the camera would still dispatch the whole target, and with the
dispatch at a few ms it would not move the frame interval either. The next thing to measure for the 4K frame
rate is the SDL_GPU present/compositor path (`src/sgp/VideoGpu.cc`), not the world renderer.

### The layer uploads are not the 4K interval either (issue #276)

The compositor uploaded each layer whole every frame: the legacy `ScreenBuffer` at the UI size (1920x1080 =
8,294,400 B at 4K world zoom 1 with UI scale 2) and, with the world renderer on the CPU, the software
`WORLD_BUFFER` (3840x2160 = 33,177,600 B) - **41,472,000 B/frame**, every frame, standing still. The layer
texture upload (`VideoGpu::UploadRgba`) now takes the rectangle to upload, and the compositor feeds it the
bookkeeping the retired SDL_Renderer path already had:

- **The UI layer** goes up in the rectangle `RefreshScreen` already computed for that path
  (`ScreenTextureUpdateRect` - the dirty `InvalidateRegion`s, the mouse cursor and the software overlay), the
  same rectangle the SDL_Renderer used to update its screen texture with.
- **The software world layer** goes up in `WorldDirty`, the union of the `InvalidateWorldRegion` rectangles the
  software renderer redrew, which the upload clears. A frame that changed nothing uploads nothing.
- **The CPU pipeline's 888 layer still goes up whole**: its rasterizer redraws the whole target every frame, so
  the whole target is what changed.
- A layer texture that had to be created or resized is filled whole whatever the rectangle, so a layer never
  starts stale; only the rectangle's rows go through the staging buffer.

The bytes are measured per layer and per frame and exposed as `ja2.videoStats()` (`{frames, uiBytes,
worldBytes}`, reset between samples), which `tests/perf/world_renderer_perf.lua` logs next to the interval.
4K real window, `--uiscale 2 --worldzoom 1` (UI 1920x1080, world 3840x2160), same counters on both sides -
"before" is the same code with the uploads forced to the whole image, which is what master does:

| 4K, world zoom 1, still | UI layer | World layer | total |
|---|---|---|---|
| before | 8,294,400 B | 33,177,600 B | **41,472,000 B/frame** |
| after, software world | 0 B | 4,056-4,077 B | **~4 KB/frame** |
| after, GPU world | 0 B | 0 B (the GPU texture is copied, not uploaded) | **0 B/frame** |

The still HUD does not change, so those 8.3 MB/frame were the same pixels every frame; the software world
changed a few hundred pixels (the item glow, an idle merc). **The frame interval did not move with the bytes**,
5 s per case, two runs each, same machine and harness (mean / median):

| 4K, world zoom 1 | before | after |
|---|---|---|
| gpu, still, cache on | 126.2 / 131.5 ms, 113.2 / 101.9 ms | 123.2 / 128.4 ms, 121.5 / 128.4 ms |
| gpu, uncached still | 111.1 / 133.1 ms, 109.2 / 132.6 ms | 89.8 / 116.9 ms, 92.1 / 99.6 ms |
| gpu, uncached scrolling | 68.3 / 100.2 ms, 131.2 / 133.8 ms | 86.7 / 98.7 ms, 106.7 / 129.5 ms |
| software world, still | 117.2 / 132.0 ms, 90.3 / 105.8 ms | 76.3 / 116.7 ms, 67.4 / 89.0 ms |

The GPU cases sit at the same ~128 ms median before and after 41 MB/frame became 0. The software case's *mean*
does land, but the software renderer is not the 4K target and its median is the same order; it also skips a
10.4 M pixel LUT pass, which is CPU work, not upload. **The layer uploads are not the 4K interval** - measured
twice over, because removing the world texture copy as well (sampling the compute texture directly, black on
D3D12 but fine for a frame interval) also leaves the interval where it was, as does a world frame the renderer
skips entirely. The next suspect is therefore further along the present path than the compositor's transfers:
the swapchain acquire/present and what the driver does with a borderless 3840x2160@59 window. The absolute
floor moved since #37 measured 99 ms on the same box (128 ms here with the machine otherwise busy), so the
A/B pairs, not the absolute numbers, are what the table is for. At 1920x1080 (UI 960x540, world 1920x1080) the
same counters read 2,073,600 B/frame (the UI layer; the GPU world is copied, not uploaded) and 10,368,000
B/frame with the software world, falling to **0 B/frame standing still** and 21-257 KB/frame scrolling, but
every case there is at the 60 fps vsync cap (16.68 ms) before and after, so this change cannot be seen at
1080p at all. Headless sessions never reach this path at all (`VideoGpu::Active()` needs a window), so the
resolution goldens and the e2e suite cannot see this change either: their green run is a regression check on
everything around it, and the proof is the before/after stills of a real window, pixel for pixel (13 stills at
1280x720, 1920x1080 and 2560x1080, plus the `--worldzoom` world-layer stills).

### The camera is free (issue #38)

The world camera (`gsRenderCenterX/Y`, `src/game/TileEngine/Camera.h`) was an `INT16` **cell** - one tile is
`CELL_X_SIZE` (= 10) of them - so there was no fraction anywhere in the type, and a pan always ended on a tile
centre. #37 made that visible: the strip cache already draws a recorded row at an offset, which is a sub-tile
camera, and the `StaticKey()` that still named the camera would have re-recorded 13 ms of world on every panning
frame. That key no longer names the camera (#37), so this issue was the small piece that was left.

**The three decisions (owner, 2026-10-15; the escalation is on #38):**

| Question | Decision |
|---|---|
| Camera ease | **A2 - eased.** A3's inertia is wall-clock dependent and would make headless runs non-reproducible |
| Resting position | **B2 - free camera, map-edge clamp only.** Coupled to A2 (with easing a snap is visible); reversible behind `Camera::kFreeCamera` |
| Mid-zoom sharpness | **C2 - at most one band behind.** One rebuild per zoom command (~13 ms), invisible at rest, pixel art stays crisp |

**Slice 1, implemented here: the camera type, the Lua surface, the tests.**

- `gsRenderCenterX/Y` are `double` cells, and `ApplyScrolling` no longer rounds the pan to the nearest tile
  centre (`Camera::kFreeCamera`); the legacy settle is `Camera::SnapToTileCentre`, one flag away. The renderer
  rounds the centre to the pixel it draws (`CalcRenderParameters`), so every blit stays at a whole pixel and
  #37's strip cache keeps translating cached rows by whole pixels - a camera move does not invalidate it.
- `ja2.camera()` is the headless surface: it reads the centre (fractional world coordinates), the tile over it,
  whether it rests on a tile centre, the world viewport and the world zoom, and `ja2.camera{x, y}` /
  `ja2.camera{grid = n}` place it. `tests/e2e/camera.lua` asserts a sub-tile centre survives a rendered frame
  (a re-introduced settle would snap it back), that centring on a tile still lands on its centre, that a pan
  moves the camera, and that the zoom the camera reports is the one `ja2.setVideo` set.
- A free camera lands a pan on a half tile, so a scroll step is half the tiles it used to be; the six
  `world_renderer.lua` scroll scenes still compare 0 px against the software renderer.

**Slice 3, implemented here: the eased camera (A2) and the slide measured.**

- `Camera::Ease` (`TileEngine/Camera.h`) is a frame-counted cubic ease-out: a discrete move - locating a merc, the
  radar, the next turn's centring - covers its distance in 12 frames and **lands exactly on the target**, which is
  the assertion the feature needs (an ease that only ever approaches is the failure). The frame count, never
  milliseconds, is what makes it reproducible in a driven run; A3's inertia is out for the same reason.
- Centring paths the player drives ease (`LocateSoldier(..., ease = true)`: the HUD's Locate command, a squad card
  click, the radar, the sector entry, the next turn); the pan and the slide stay direct, a pan or slide cancels an
  ease in flight, and an ease a menu freezes (`gfIgnoreScrolling`) is cancelled rather than left in flight. The
  callers that read the merc's screen position immediately (the door menu, the item and dialogue panels) keep the
  immediate locate. `ja2.camera()` reports `target`/`animating`/`settled`, `{ instant = true }` places at once and
  `{ slideTo = n }` starts the game's slide, and `NothingInFlight()` counts an ease so a screenshot cannot race it.
- **The auto-slide was measured, not tuned by eye** (`tests/e2e/camera.lua`): at the sub-tile step
  `SoldierLocationRelativeToScreen` only ever closes in on the merc - no frame moves away from him - and the slide
  ends in bounded time. Its 20 px dead zone is the same size as the game's scroll step, so a step can leave the merc
  at most that far off centre and cannot oscillate; it is left as it is, with the measurement as the evidence.
- **A pre-existing freeze fixed on the way:** `gfIgnoreScrollDueToCenterAdjust` was cleared only by the software
  presenter, so on a layered screen (every world renderer, and every driven run) the first `SetRenderCenter` left it
  set and `ScrollWorld` returned for the rest of the session - keyboard scrolling froze, and the slide above was
  unreachable to test. `ScrollWorld` now spends the flag at the one place that reads it.
- **Goldens did not move.** The ease changes what is on screen *during* a move, but it lands where the old jump
  landed, so every settled scene (the 1080p resolution suite and the smoke scenes) is pixel-identical; the proof is
  the passing suite and the `--changed` e2e run, not a regenerated image.
- **A crash met on the way is filed, not fixed:** a zoom step that lands in a slide's last frame corrupts the heap
  (`0xc0000374`/`0xc0000005`) - the slide is the legacy per-frame step and the video teardown was always reachable
  while a scroll settled, but slice 3's test is what walked into it. #399 carries the repro; the test waits for idle
  before its video change.

**The 3840x2160 re-run, for the record.** `tests/perf/world_renderer_perf.lua` on the machine that measured #393's
floor (Ryzen 5 5560U, D3D12, borderless 3840x2160 @ 59 Hz, `--uiscale 2 --worldzoom 1`, 5 s per case): gpu cached
still mean 72.7 / median 96.8 / p95 103.9 ms; gpu uncached still 83.6 / 97.9 / 103.2; gpu cached scrolling
47.5 / 17.1 / 99.9; gpu uncached scrolling 100.3 / 99.4 / 104.6. That is the same order as #393's ~128 ms
present-path floor, and it keeps the bimodality #393 describes (some frames at the 16.7 ms vsync, the rest stuck at
the floor). **The camera work is not the 4K interval and no frame-rate win is claimed for it:** a centring move
adds nothing to a scroll frame, and the harness scrolls with the keys, which stay direct. The number is here so the
next person does not mistake this change for a frame-rate one.

**Left: slice 2, the animated zoom.** C2 - the raster scale split from the present zoom, so a zoom step animates
the present scale with at most one band of raster behind instead of rebuilding the world layer whole,
`m_worldRasterQ` / `PresentScale` - is the one part of A2/B2/C2 this change does not land. Slice 4 (this page and
`docs/ui/tactical.md`) and the 3840x2160 re-run above are done.

## Gaps

- The static cache keeps its strips across camera moves (#37) and the GPU dispatches only the region that
  changed, not the whole target (a dispatch origin; see *The dispatch origin, and what the 4K floor actually
  is*). The compositor now uploads only the dirty part of each layer too (#276, *The layer uploads are not the
  4K interval either*). Neither moves the 4K frame interval on the machine they were measured on: with every
  layer upload removed, and with the world texture copy removed as well, it is still ~128 ms. The next
  measurement belongs further along the present path (the swapchain acquire/present). The camera-move
  frame shift (a texture-to-texture blit of the retained region) is not done, and is not what limits the frame.
- The camera is free and centres eased (#38, *The camera is free*): it no longer rounds every pan to a tile centre,
  a discrete centring move glides to its target in 12 frames and lands on it exactly, and `ja2.camera()` reads the
  centre, the move and the slide out. The 20 px auto-slide is measured rather than tuned and does not chatter at the
  sub-tile step. **Still open: the animated zoom** - a zoom step rebuilds the world layer whole; the raster scale is
  not yet split from the present zoom (C2's *at most one band behind*). A zoom step in a slide's last frame also
  crashes (#399).
- MSL is generated but not compiled or run (no Mac).
- The screenshots of driven windows are composed from the read-back world buffer with nearest scaling; the player
  sees the GPU texture scaled with SDL's pixel-art filter.
- Night vision has no separate render path in the game (lighting is the shade palettes, covered). Explosions are
  anitiles and go through the recording; there is no live-explosion scene in the equivalence test.

# Save format

Decisions that change what a saved game holds. The [Dev mindset](../../AGENTS.md) applies: we own the save format,
the revamp is the game, and there is no vanilla mode and no byte-compatible save to preserve.

| Question | Decision |
|---|---|
| The map-screen message log's game minute and kind (issue #43) | **Both are saved**, in the per-message record the log writes after each message's text (`MapScreenMessageRecord.h`, used by `Message.cc`). The log is rebuilt from the file on load, so without them every loaded message came back with `gameMinute == 0` and showed under **Earlier** with no clock time, and `priority == 0` lost the kind that puts a merc's dialogue under the log's **Team** tab. The record's size and field order live in one place (the writer and the reader used to spell the same layout out twice; a disagreement would silently shift every message after the first) |
| What "kind" means | **`ubPriority`**, the `MSG_*` constant the message was posted with (`MSG_INTERFACE`, `MSG_DIALOG`, ...). There is no separate message-class notion: that is the value `MapModel::Classify` reads (`it->priority == MSG_DIALOG`) to classify a line as Team, and the one `MapScreenMessage` and `ScreenMsg` already carry. The log's other two kinds are not a stored field: Combat is the message's colour (red) and Money is its text (it names a sum) |
| Saves written before this | **Not migrated and not read.** An older save does not round-trip the two fields, and nothing tries to make it: there is no version gate, no fallback that keeps reading the 28-byte record, and no shim. An old save simply loads with the log's game minutes unknown (as every save did before), which is the intended outcome; the project does not carry compatibility code for saves it did not write |

# Test infrastructure: the resolution matrix (issue #45)

The matrix is the 36 tours at 4 resolutions — 144 tests. `ctest -L resolution` is the default
1080p-only sweep (`tests/e2e/README.md`), `ctest -R resolution_` the full matrix, `ctest -L matrix`
the three non-1080p tiers. Everything below was measured on 2026-10-15, with the box otherwise
idle: **bagong** is the Ryzen 5 5600 desktop (12 threads), **the laptop** the Ryzen 5 5560U dev
machine that normally drives the agent.

## A failing tour says which layer failed

A resolution test runs a tour and then compares its shots, so a nonzero exit can come from three
places. It used to print `script failed with exit code 4294967295` for all of them — including the
case where the tour's own log said `passed`. `0xffffffff` is -1, what PowerShell's `Stop-Process`
and .NET's `Process.Kill` leave behind, so it read as a crash and a reader had to guess between the
script, the golden compare and the process. Now the tour's own verdict decides, not the code — the
driver prints `passed` or `FAILED` for every result the script produced, so a *missing* verdict is
itself evidence — and each layer has its own message:

| Message | What failed |
|---|---|
| `the script failed (exit code N: …)` | the tour: a failed expectation or check, or the game throwing |
| `the tour ended before a verdict (exit code N: …)` | the tour, before it could report: a bad path or save, a crash at startup, a wait or the watchdog timing out |
| `the golden compare failed, the script did not:` | the pixels |
| `the script passed and the process died after it …` | the process, after a tour that passed |
| `the process died before the script reached its verdict …` | the process — a kill or a crash, never the script |

The last message covers exit 1 with no `FAILED` line: the driver prints one before every exit 1, so
an exit 1 without a verdict is not the driver's own result, and a force-kill can use any code, which
is why the verdict decides — Windows' `taskkill /F` terminates with code 1, which is also "a script
expectation failed". The golden compare is the only failure with an exit code of the harness's own
(`5`, `check_resolution.py`); the rest propagate the driver's or the process's.

`tools/ja2ctl.py` owns the exit-code vocabulary (the driver's 0–4 are `Automation::ExitCode`; there
is no driver code outside them) and describes anything else as an external kill, an unhandled
exception, a signal or an unknown code. `tests/e2e/check_resolution.py` keeps the tour's own
`passed`/`FAILED` line and uses it to name the layer. Reproduce the process rows by killing the game
process of a running tour (`Stop-Process -Force` reports 0xffffffff, `taskkill /F` reports 1); the
tour row with any script that fails. `tools/tests/test_exit_codes.py` pins all of it, including that
the wording it greps for is still what `Automation.cc` prints.

## The 3440x1440 watchdog: the whole tier gets 2400 s

Per test the watchdog is now 900 s by default, 2400 s for every map screen tour (since #24) and for
**every 3440x1440 tour** (this issue). The tier rule is worth spelling out, because the obvious
per-tour reading — raise the tours that measured close to 900 s — is not what the numbers support.

The one timeout the tier has produced is `menus_tour` at **900.37 s of its 900 s** under `-j8`, and
that tour costs **78 s on bagong and 106 s on the laptop** in a quiet `-j8` run: on the machine that
hit the watchdog it measured **411.77 s** alone, and it still hit 900 s in parallel. That is a 4–8x
spread for one tour across runs and machines, so a quiet-run cost does not predict which tour needs
the headroom — and the tier's cost ranking does not match the incidents either (`tactical_popups`
is the most expensive tour at this resolution and never timed out; `menus_tour` is mid-table and
did). On a tier whose worst tour finishes in 184 s (bagong) / 367 s (laptop), the 900 s default
buys a false failure as soon as the machine is loaded; 2400 s costs only a delayed report if a tour
genuinely hangs, and it is the value #24 already uses.

The other three resolutions keep 900 s: the measured maximum on the laptop at `-j8` is 183 s
(1080p) and their pixel counts are well below 3440x1440's.

### The audit: every 3440x1440 tour, one `-j8` run per machine

`ctest -R resolution_.*_3440x1440 -j8`. Both runs passed 36/36; wall time 363.23 s (bagong) and
610.00 s (laptop). The timeout column is what the tour had before this issue; 2400 for the map
screen tours was #24's.

| 3440x1440 tour | timeout before | bagong `-j8` | laptop `-j8` | laptop margin |
|---|---|---|---|---|
| tactical_popups | 900 | 179.16 | 366.71 | 2.5x |
| laptop_parity | 900 | 146.02 | 260.26 | 3.5x |
| world_map_steps | 900 | 109.93 | 244.91 | 3.7x |
| mapscreen_parity | 2400 | 184.04 | 243.28 | 3.7x |
| world_map_actions | 900 | 97.04 | 211.47 | 4.3x |
| options_parity | 900 | 158.10 | 207.15 | 4.3x |
| tactical_overhead | 900 | 93.45 | 163.06 | 5.5x |
| saveload_parity | 900 | 117.19 | 155.75 | 5.8x |
| tactical_overlays | 900 | 74.47 | 116.55 | 7.7x |
| menus_tour | 900 | 78.32 | 106.33 | 8.5x (900.37 s used on the reporter's machine) |
| mapscreen_overlays | 2400 | 69.22 | 92.93 | 9.7x |
| mapscreen_states | 2400 | 65.01 | 92.50 | 9.7x |
| mapscreen_equivalence | 2400 | 62.11 | 87.83 | 10.2x |
| tactical_parity | 900 | 59.84 | 84.47 | 10.7x |
| video_switch | 900 | 55.76 | 74.39 | 12.1x |
| autoresolve_parity | 900 | 54.69 | 72.00 | 12.5x |
| sector_stock | 900 | 47.37 | 68.34 | 13.2x |
| weapon_readout | 900 | 40.95 | 65.16 | 13.8x |
| tactical_hud | 900 | 46.39 | 63.49 | 14.2x |
| loadout_screen | 900 | 41.80 | 56.57 | 15.9x |
| shopkeeper_parity | 900 | 37.03 | 55.47 | 16.2x |
| new_game_to_tactical | 900 | 33.62 | 50.01 | 18.0x |
| tactical_commands | 900 | 29.05 | 44.11 | 20.4x |
| map_screen_tour | 900 | 30.06 | 42.29 | 21.3x |
| newgame_parity | 900 | 32.55 | 41.77 | 21.5x |
| tactical_move_merc | 900 | 29.98 | 41.10 | 21.9x |
| people_content | 900 | 28.77 | 38.31 | 23.5x |
| campaign_state | 900 | 28.89 | 37.31 | 24.1x |
| native_ui | 900 | 22.35 | 27.87 | 32.3x |
| mainmenu_parity | 900 | 19.42 | 24.93 | 36.1x |
| laptop_tour | 900 | 15.09 | 19.87 | 45.3x |
| credits_parity | 900 | 15.03 | 19.83 | 45.4x |
| loadingscreen_parity | 900 | 14.23 | 18.94 | 47.5x |
| epilogue_parity | 900 | 13.35 | 17.69 | 50.9x |
| main_menu | 900 | 5.07 | 6.48 | 138.9x |
| intro_parity | 900 | 3.90 | 5.70 | 157.9x |

Laptop margin is 900 s over the laptop's measured time: how much headroom the default watchdog
gave a quiet run. Every row is above 2.4x, and one of the two tours that ever hit the watchdog is
at 8.5x — which is the point above: the margin is a property of the run, not of the tour.

### The full matrix, green in one run

`ctest -R resolution_ -j8` on bagong, twice, one run each: **144/144 passed both times — 910.78 s of
wall time at `268eb54b8`, and 827.10 s at `d0cd839b0`** (the build that carries the reporting fix;
5292.81 / 5180.82 s\*proc under the `matrix` label, 1504.46 / 1425.70 s\*proc under `resolution`).
Each is a single run — not a re-run until green: no test needed the new watchdog and nothing was
retried. No golden image drifted in either (`git status tests/e2e/golden` empty). The same commit's
e2e suite is 59/59 in 288.75 s, also `-j8`; `tools/tests/test_exit_codes.py` (17 tests) and
`ctest -L tooling` cover the reporting change.

The same day, for the other tiers: the laptop's 1080p sweep (`ctest -L resolution`, 36 tests) is
36/36 in 317.16 s and its 3440x1440 subset 36/36 in 610.00 s, both `-j8`.

## What "green" means for this matrix

Green is one run in which no test failed *as a test*: no failed expectation, no golden compare
failure, no watchdog. Nothing in the suite is currently flaky in that sense — the four runs above
(36 + 36 + 36 + 144 tests) had no such failure on machines with the box otherwise idle. What the
matrix does produce on a machine that is doing something else is two other things:

- **A timeout.** The default 900 s was too close at 3440x1440 once the machine was loaded, which is
  what the section above fixes: with the tier at 2400 s, a slow run has headroom and a hung run is
  still reported.
- **A killed process.** Issue #45's `script failed with exit code 4294967295` and the orchestration
  note's two failures are all `0xffffffff` = -1, which no code in this repository emits — the
  driver's own codes are 0-4 and the harness's own is 5 for a golden compare, so anything else came
  from the process (verified: `Stop-Process -Force` on the game process reproduces 4294967295, and
  the tour's own log line said `passed` both times; Windows' `taskkill /F` uses 1 instead, which is
  the case the verdict — not the code — catches). That is an external kill of the game process — an
  agent harness or a tool reaping a process tree — not a test result, and it is not retried: the
  harness now reports it as the process layer, so the run can be judged as "not a test failure" or
  repeated *whole*, with a reason.

So the honest operating rule: run the sweep with the machine to itself (bagong's `dev.py status`
`active: 0` is the gate), and read a failure by the layer it names. A matrix that is green in one
run under contention is not a target worth a retry policy; a matrix that reports *what* failed is.

