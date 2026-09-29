# Native HD Assets — Everything Sharp at Modern Resolutions

> **Status: proposal.** This follows [graphics-remaster.md](graphics-remaster.md) (Phases 0–5,
> implemented). That plan made the game *fill* a modern display. This one makes it *look native*
> there. Gameplay, data formats, save games and the classic 640x480 mode stay as they are.

## Goal

At 1440p or 4K the game currently draws 1990s art at 1x and scales it up with nearest-neighbour
(UI scale `Su`, world zoom `Zw`). It is sharp, but it is visibly blocky: fonts are 8–12 px
bitmaps, icons are 16–32 px, and portraits are 48x43. The goal is that **every pixel on screen
is drawn at the display's physical resolution**:

1. **Text** is rendered from outline fonts at the physical pixel size.
2. **UI chrome** (panels, buttons, frames, bars, cursors, icons) comes from HD assets. It can be
   hand-made, vector-drawn, procedural or upscaled, and it is picked by scale.
3. **Content art** (faces, item pictures, laptop pages, load screens, radar maps) comes in HD
   versions.
4. **The tactical world** (tiles and animations) can use HD versions too. This is the biggest
   item and the last phase.
5. **Missing HD assets fall back** to today's scaled 1x art, one asset at a time. Every phase can
   ship on its own, and a half-finished pack still works.

Out of scope: new gameplay content, changes to art direction beyond "the same thing, sharper",
and 3D.

---

## Where things stand today (research)

### Asset inventory (JA2 Gold, `Data/*.slf`)

| Archive | Entries | Mostly | What it is |
|---|---|---|---|
| `Tilesets.slf` | 6,324 | 4,978 STI, 1,266 JSD, 60 PCX | World tiles, structures, terrain |
| `Anims.slf` | 645 | 563 STI, 64 JSD | Soldier, creature and vehicle animations |
| `Faces.slf` | 710 | 706 STI | Merc/NPC portraits (big and small), eyes/mouth frames |
| `Radarmaps.slf` | 331 | 330 STI | Per-sector radar minimaps |
| `Laptop.slf` | 293 | 282 STI, 3 PCX | Laptop OS and all web pages (AIM, MERC, IMP, Bobby Ray, …) |
| `Bigitems.slf` | 245 | 244 STI | Large item pictures (item description box) |
| `Interface.slf` | 202 | 186 STI, 13 PCX | HUD, map screen, menus, buttons, panels |
| `Data.slf` | 101 | 72 STI, 2 PCX | Misc UI, lights, item graphics |
| `Cursors.slf` | 69 | 68 STI | Mouse cursors (many animated) |
| `Loadscreens.slf` | 47 | 46 STI | Full-screen loading art |
| `Fonts.slf` | 24 | 23 STI | All 16+ bitmap fonts |
| `Intro.slf` | 11 | 11 SMK | Intro and ending videos |

That is roughly **7,500 image files, many with dozens of frames**. The tactical world (tiles and
animations) is about 75% of the files and far more of the frames. Everything else ("UI and
content") is about 1,900 files.

### The engine side

- **Loading.** `src/sgp/HImage.cc` `CreateImage()` dispatches by extension to `STCI.cc` (STI,
  8-bit palettised or 16-bit, multi-frame with per-frame offsets and an "app data" block) and
  `PCX.cc`. There is no PNG/RGBA loader. `stb_image_write.h` exists for screenshots only.
- **Objects.** `VObject.cc` (`SGPVObject`) holds the frames and palette. Its blitters in
  `VObject_Blitters.cc` (42 of them) draw 8-bit → RGB565 with Z, shadow, outline and translucency.
  None of them scale, and all of them assume 1 image pixel = 1 surface pixel.
- **Layers.** Since remaster Phase 4 there is a UI layer at `Su` and a world layer at `Zw`, both
  RGB565 and both drawn at *logical* size, then scaled up by the GPU. **Neither is at physical
  resolution**, so an HD asset has nowhere to go yet. This is the first engine change.
- **Fonts.** `Font.cc` treats each glyph as a frame of an 8-bit STI. `Font_Control.cc` maps
  font IDs such as `FONT10ARIAL`, `BLOCKFONT` and `TINYFONT1` to STIs. Colours come from palette
  remapping (`SetFontForeground`/`Shadow`), and layout uses glyph widths in logical pixels.
- **VFS.** It is layered: the `externalized/` and mod directories override `.slf` entries by path.
  This is where HD overrides live, and nothing new is needed to *find* a file.
- **Automation.** `ja2ctl` screenshots, golden images per resolution, `ja2.assertInsideScreen()`
  and the `ja2.debug(...)` hooks make screen-by-screen visual review practical.

### What this means

| Want | Gap |
|---|---|
| HD asset drawn at native pixels | A UI render target at physical resolution; blits that take logical coordinates and an asset scale |
| HD assets on disk | A PNG/RGBA loader with frames, offsets and metadata, and a naming convention (`@2x`, `@4x`) |
| Sharp text | A TTF glyph renderer behind the existing font API, with metrics that still fit the old boxes |
| Transparent, smooth-edged art | An alpha (32-bit) path in the blitters, at least for the UI layer |
| HD world | 32-bit world blitters, shade tables and lighting in 32-bit, and a Z-buffer at HD scale |
| Lots of art | A repeatable production and review pipeline (the main subject of this plan) |

---

## Target architecture

```
 UI code (logical coords, unchanged)          World code (unchanged coords)
        │                                              │
        ▼                                              ▼
 UI layer: ARGB8888 at logical × Su            World layer: RGB565 → ARGB8888 at world × Zh
 (physical resolution)                          (Zh = HD world asset scale, Phase 6)
        │  Blt(obj, x, y) → dest (x·Su, y·Su)          │
        │  asset = best of {@4x, @2x, 1x}; resample    │
        │  only when asset scale ≠ Su                  │
        └───────────── Compositor (existing, Phase 4) ─┘
```

Key decisions:

1. **Coordinates stay logical.** Mouse regions, layouts, externalized JSON and mods all keep
   their 640x480-style units. Only the *pixels* get denser. That keeps the thousands of hand-placed
   offsets working.
2. **Asset lookup is by scale.** For `interface/panels.sti` the engine looks for
   `interface/panels@4x.png` then `…@2x.png`, then falls back to the 1x STI. The best one ≤ `Su`
   is drawn exactly. If only a smaller one exists it is scaled up (nearest, or sharp-bilinear for
   non-integer scales). If only a larger one exists it is scaled down (area filter).
3. **HD images are PNG with a sidecar JSON.** Multi-frame images become one PNG per frame, or a
   sheet plus JSON. The JSON holds the frame rects, per-frame offsets (already multiplied by the
   scale) and any STI app data. PNG is editable by every tool, diffable by the golden-image
   tooling, and loads through stb_image.
4. **Classic mode is untouched.** At `Su = 1` with no HD pack, the old 8-bit path and all
   640x480 goldens must stay pixel-identical.
5. **The pack is separate from the engine.** The engine code, tools and specs live in this repo.
   The art lives in an HD pack directory (VFS layer) whose distribution is settled per asset
   class (see *Licensing*).

---

## Methodology: how assets get recreated

This is the reusable part. Every content phase below uses the same loop.

### M1. Inventory and usage map (per screen)

- `tools/assets/extract.py` exports every STI/PCX to PNG plus JSON (frames, offsets, palette,
  app data) into a work tree mirroring the VFS paths.
- An automation hook records **which assets each screen loads and draws** (log `CreateImage` and
  blits per screen ID). Running the e2e tours then gives *screen → asset list*. This decides
  priority: an asset seen on every tactical frame matters more than one AIM page background.
- `assets/manifest.json` lists every asset with its class, screens, frame count, size, chosen
  recreation method, status (`todo`/`draft`/`review`/`done`) and source/licence. A small HTML
  report generated from it shows progress.

### M2. Pick a recreation method per asset class

| Method | How | Best for | Quality / cost |
|---|---|---|---|
| **Replace** | A modern equivalent: open-licence TTF fonts, system-like cursors | Fonts, some cursors | Best result, cheapest; needs metric matching |
| **Procedural** | Generated in code or a script: 9-slice frames, bevels, gradients, rivets, flat panels, bars | Panel chrome, frames, bars, buttons, fillers | Perfectly sharp at any scale, tiny files; needs a UI-kit spec |
| **Vector redraw** | Traced or hand-authored SVG, rasterised at 2x/4x by a script | Icons, symbols, map markers, logos, simple buttons | Sharp and consistent; per-asset effort |
| **Upscale + cleanup** | A pixel-art-aware upscaler (e.g. ESRGAN-family models tuned for sprites, or xBRZ/ScaleFX as a baseline), then an alpha/edge fix and palette re-quantise if needed, then manual touch-up | Portraits, item pictures, load screens, laptop pages, radar maps, tiles and animations | Fast at volume; results vary, so it needs review; frames must stay consistent |
| **Hand repaint** | An artist paints over the upscaled draft | The most-seen assets where upscaling looks mushy (merc faces, main HUD) | Best for hero assets; slow; needs a human artist |

Rules of thumb:
- **Text is never upscaled.** It is always Replace.
- **Anything flat or geometric** is Procedural or Vector, not upscaled.
- **Photographic or painted art** is Upscale + cleanup, promoted to Hand repaint only if review
  says so.
- **Animated sprites** are upscaled per frame with the same model and settings. They are
  checked for temporal consistency (no shimmer between frames) and keep their offsets multiplied
  by the scale.

### M3. Specs before pixels

For each screen or class, write a short spec first: target scales (2x and 4x, where 4x means 4K
at `Su = 2` or 1440p/4K at `Su = 3`), palette/colour intent, stroke widths, corner radii, 9-slice
insets, shadow style. This keeps a screen consistent when several agents or artists work on it.
A shared **UI kit spec** (`docs/hd/ui-kit.md`) holds the common parts: bronze/leather panel
material, bevel, button states, scrollbar, checkbox, and the font scale.

### M4. Automated quality gates (tests, not taste)

`tools/assets/check.py` runs in CI on the pack:
- **Geometry:** HD size = 1x size × scale exactly, frame counts match, and offsets = 1x offsets ×
  scale.
- **Silhouette:** the HD alpha, downscaled to 1x, must match the 1x transparency mask (IoU above a
  threshold). This catches mis-registered or cropped art, which would break hit-testing and
  layering.
- **Colour drift:** the HD image downscaled to 1x must be close to the original (mean ΔE below a
  threshold), so upscalers don't shift the palette.
- **Frame stability** (animations): the difference between consecutive HD frames must be about
  the same as between consecutive 1x frames × scale. This catches shimmer.
- **Budget:** file size per class, and total pack size.

### M5. In-game review (visual proof, see [AGENTS.md](../../AGENTS.md))

- Run each affected screen at 1080p (`Su = 1`, native), 1440p (`Su = 2`) and 4K (`Su = 2` and
  `3`), with and without the pack (`-hdpack off`).
- `tools/assets/compare.py` builds side-by-side and zoomed crop sheets (old vs new, same framing)
  from `ja2ctl shot` output. These go in the PR.
- Golden images: add a `hd` dimension to the resolution matrix (`golden/<res>@hd/`). Classic
  goldens stay untouched.
- **The reviewer opens the images.** The acceptance question: "Does this look like the original
  game, just sharper? Does anything look out of place next to its neighbours?"

### M6. Screen-by-screen delivery

A content PR covers **one screen or one asset class**: assets, manifest updates, the check
report, and comparison sheets. Order by the usage map (M1): most-seen first.

### What agents can and cannot do

- **Agents can:** extract, catalogue and track assets, write the loaders and renderers, author
  procedural and SVG assets, run upscaling pipelines (Python + ONNX/PyTorch on CPU is slow but
  fine offline; a GPU machine is much faster), run the quality gates, drive the game and produce
  the review sheets.
- **Agents are weak at:** painterly hand repaints and judging subtle art style. Hero assets (merc
  faces, main HUD chrome, possibly tiles) may need a human artist, or an image-generation tool
  that a person curates. The plan keeps these as explicit, optional *Hand repaint* tasks, and the
  game looks fine at every stage without them.

---

## Phases

| # | Phase | Kind | Rough size | Result |
|---|---|---|---|---|
| 0 | Asset tooling and usage map | tools | ~1.5k LOC | Extractor, manifest, per-screen usage map, check/compare tools |
| 1 | HD UI render path | engine | ~3–4k LOC | UI layer at physical resolution, PNG/RGBA loader, `@Nx` lookup, alpha blits, 9-slice |
| 2 | Outline fonts | engine + content | ~2k LOC | All text sharp at any scale |
| 3 | Cursors and icons | content | ~200 assets | Sharp cursors, item/status/map icons, buttons |
| 4 | UI chrome, screen by screen | content | ~400 assets, many PRs | Tactical HUD, map screen, menus, dialogs, popups in HD |
| 5 | Content art | content | ~1,300 assets | Faces, big item pictures, laptop/web pages, load screens, radar maps |
| 6 | HD world | engine + content | ~5k LOC + ~5,500 assets | 32-bit world path, HD tiles and animations |
| 7 | Cinematics (optional) | content + engine | small | Upscaled intro/ending videos |

Phases 0 → 1 → 2 are sequential. Phases 3, 4 and 5 can run in parallel after 1, and 4 benefits
from 2. Phase 6 needs 1 and is independent of 3–5. Phase 7 is independent.

---

### Phase 0 — Asset tooling and usage map

- `tools/assets/extract.py` (Python, reads from the game dir through the same VFS rules, or via
  the Rust `stracciatella` crate's SLF reader exposed as a small CLI): STI/PCX → PNG + JSON, all
  frames, into `_work/assets/1x/<vfs path>`.
- Automation: `ja2.assetLog(true)` records `CreateImage` loads and the objects actually blitted,
  keyed by screen. `tools/assets/usage.py` runs the e2e tours with it and writes
  `assets/usage.json` (screen → assets, with a draw count).
- `assets/manifest.json` + `tools/assets/report.py` → an HTML progress page grouped by class and
  screen.
- `tools/assets/check.py` (the M4 gates) and `tools/assets/compare.py` (the M5 sheets), with unit
  tests on synthetic images.

**Done when:** a single command produces the inventory and usage map for the installed game, and
the report shows all ~7,500 assets classified with a proposed method.

### Phase 1 — HD UI render path

- **UI layer at physical resolution.** The UI surface becomes ARGB8888 at `logical × Su`.
  Surface-level APIs (`BltVideoObject`, `BltVideoSurface`, `ColorFill…`, `Shadow…`, rectangles,
  lines) keep taking **logical** coordinates and multiply by `Su` internally. Mouse regions
  don't change.
- **Legacy 1x assets.** Keep the existing 8-bit blitters writing into a scaled path: blit at 1x
  into a scratch buffer, then nearest-scale into the HD surface. As a faster alternative, add
  integer-scaling variants of the hot UI blitters. At `Su = 1` it stays today's RGB565 path, so
  classic mode is pixel-identical.
- **Loader.** `HImage` gains PNG (stb_image) + JSON sidecar and multi-frame sheets. `SGPVObject`
  gains an RGBA frame store and a `scale` field. The VFS lookup tries `name@{Su..1}x.png` before
  `name.sti`, with a `-hdpack <dir>` / `hd_pack` config option to enable or disable the lookup.
- **Blitters.** Add alpha blending, alpha + shadow, and outline/highlight for RGBA sources on the
  UI layer. Palette remaps used for UI (e.g. button disabled/greyed) become tint parameters.
- **9-slice panels.** `Blt9Slice(obj, box, insets)` for procedural and HD frames that stretch to
  any size. Phase 4 widescreen fillers use it.
- **Save/restore.** The background save and restore buffers (`guiSAVEBUFFER`, video overlays,
  `RestoreExternBackgroundRect`) must work at physical resolution on the UI layer.
- **Screenshots and tests.** `ja2ctl shot` captures the physical-resolution composite. The
  resolution matrix gains `@hd` runs using a tiny test pack of 3–5 hand-made assets.

**Done when:** with the test pack, those assets draw natively sharp at `Su = 2` and `3`, and
everything else looks as before. Classic goldens are unchanged, and frame time at 4K is within
1.2x of today.

### Phase 2 — Outline fonts

- Add SDL_ttf (or stb_truetype). `Font.cc` gets a glyph-cache backend: rasterise at
  `pt × Su` and cache per (font, size, scale, colour).
- **Mapping.** Each legacy font ID maps to a TTF face and a *logical* pixel size tuned so that
  advance widths and line heights match the bitmap font closely. Text must still fit its old
  boxes, so check wrapping in `WFGetFontHeight`/`StringPixLength` users. Mapping lives in
  `externalized/fonts.json` and is moddable.
- **Style.** Foreground, shadow and background colours come from the existing `SetFont*` calls.
  The "blocky" title fonts (`BLOCKFONT`, `HUGEFONT`) get a stylised face or stay as HD bitmap
  fonts (Phase 3 method).
- **Font choice.** Open-licensed only (OFL/Apache, e.g. a condensed sans for UI, a monospace
  for the laptop terminal, a stencil/military display face for titles). Ship them in the repo.
- **Language support.** Keep today's language coverage (Russian, Polish, German, and more).
  Pick faces with the glyphs, and keep the bitmap fallback per language if needed.
- **Review.** Every screen with text, at 1080p/1440p/4K. Look for overflowing strings.

**Done when:** all text is outline-rendered at `Su ≥ 2`, no string overflows its box in the tour
screens, and the region audit passes. Classic mode can keep bitmap fonts.

### Phase 3 — Cursors and icons

- **Cursors** (`Cursors.slf`, 68, many animated): vector redraw at 2x/4x, with the hotspots
  multiplied by the scale. Drawn at physical resolution by the cursor layer.
- **Icons:** the inventory item graphics used in pockets (small item images from `Data`/
  `Interface`), status icons, map-screen markers (merc, militia, enemy, helicopter), and
  assignment/button glyphs. Vector redraw where they are symbolic. Upscale + cleanup where they
  are pictorial (item pictures).
- **Buttons:** generic button art as procedural 9-slice with states (normal, hover, pressed,
  disabled).

**Done when:** the cursor and every icon on the tactical HUD, the inventory and the map screen are
HD, as shown by comparison sheets, and the M4 gates pass.

### Phase 4 — UI chrome, screen by screen

One PR per screen, in usage order. Each one: spec (M3) → assets → gates → sheets.
1. Tactical bottom bar (team panel, single-merc inventory panel, radar frame, widescreen fillers
   as 9-slice)
2. Map screen (character list/info, map border, bottom bar, popups)
3. Message boxes, popups, tooltips, context menus
4. Main menu, options and video options, save/load, game-init options
5. Pre-battle, auto-resolve, sector inventory, shopkeeper, item description box
6. Editor taskbar (last, lowest priority)

Backgrounds that are painted art (the main-menu flag, options backdrop) use Upscale + cleanup.
Frames, bevels and bars use Procedural, from the UI-kit spec.

**Done when:** each listed screen at 4K has no visibly blocky element apart from world art and not
yet converted content art. Each PR carries comparison sheets.

### Phase 5 — Content art

Mostly Upscale + cleanup, at volume, in batches per class:
- **Faces** (706): big and small portraits plus eye/mouth animation frames, which must stay
  aligned, so run the frame-stability and silhouette gates. Hero mercs are Hand repaint
  candidates.
- **Big item pictures** (244), shown in the item description box and shops.
- **Laptop and web pages** (282): page art, logos and photos. Text in the art is re-typeset with
  Phase 2 fonts where it is actually rendered text. Baked-in text is upscaled, and redrawn if it
  is unreadable.
- **Load screens** (46) and **radar maps** (330). For radar maps, consider *regenerating* them
  from the sector maps at HD scale instead of upscaling.

**Done when:** each class is complete in the manifest, the gates pass, and a review sheet per batch
is accepted.

### Phase 6 — HD world

The largest phase. Split it into engine and content sub-phases.
- **Engine.** Add a 32-bit world path: ARGB8888 world surface at `world × Zh` (HD asset scale),
  RGBA tile and animation blitters with Z-buffer, shadows, lighting (shade tables → multiplicative
  tint in 32-bit), translucency and outlines. `Zh` is an asset scale independent of `Zw` (world
  zoom). Keep the 8-bit path for classic. Profile early: a 4K world is 8.3 Mpx per full redraw.
  Keep dirty-rect rendering, and SIMD the hot blitters.
- **Coordinates and structure data.** JSD structure data (collision/heights) stays at 1x; only
  pixels scale. Tile picking uses 1x structure data through the existing world transform.
- **Content.**
  - Tiles (4,978 STIs across tilesets): upscale per tileset with consistent settings. Tiles must
    seam cleanly, so upscale on padded or wrapped sheets and crop afterwards. Check seams by
    rendering sample maps.
  - Animations (563): per-frame upscale with the frame-stability gate. Palette-swapped soldier
    colours (hair, skin, vest, pants) must keep working, so upscale the *index* layer separately
    or keep a mask-based recolour.
- **Review.** Render fixed camera positions in a set of reference sectors (Omerta, Drassen, a
  forest, a mine, an interior, night) at 1x and HD. Build comparison sheets.

**Done when:** the reference sectors render with HD tiles and HD mercs at 4K within the frame-time
budget, and seams and animation shimmer pass the gates.

### Phase 7 — Cinematics (optional)

Upscale the 11 Smacker videos offline (frame extraction → upscaler → re-encode). Play them
through a modern decoder path, or keep Smacker at a higher resolution if the decoder supports it.
Fall back to the scaled original.

---

## Licensing and distribution

The original art is owned by the JA2 rights holders. Stracciatella ships no game data, and this
plan keeps it that way.
- **In this repo:** the engine code, tools, specs, the UI-kit, procedural generators, open-licence
  fonts, and **original** vector/procedural assets made from scratch.
- **Derived assets** (upscaled or repainted originals) are **not committed**. Two options, to be
  decided before Phase 3:
  1. **Generate locally.** A deterministic pipeline (fixed model, weights and settings) that turns
     the player's own game files into the HD pack on their machine: `ja2 --build-hd-pack`. Nothing
     derived is redistributed. Hand-repaint work can't be shipped this way.
  2. **Separate pack.** A separate HD-pack repository or download with its own licensing
     question, similar to how other remaster mods are distributed.
- Every manifest entry records its method and source so this can be audited.

## Cross-cutting concerns

- **Mods.** Mods can override HD assets the same way (`@2x.png` in their directory). The manifest
  and check tools work on any pack directory.
- **Performance and memory.** HD assets at 4x use 16x the memory (and RGBA doubles it again), so
  load lazily and evict unused screens. Set budgets in the check tool, and measure load times on
  a low-end machine.
- **Headless and tests.** Headless can load the pack (CPU path) so goldens cover HD. CI uses a
  small committed test pack of original assets, not derived ones.
- **Android and macOS.** Phase 1 needs the RGBA path on both. Check memory on Android.
- **Runtime switching.** Changing `Su` (remaster Phase 5 video settings) re-picks asset scales and
  flushes caches.

## Main risks

| Risk | Mitigation |
|---|---|
| Upscaled art looks mushy or inconsistent | Method table (upscale only painted art), M4 gates, M5 review sheets, Hand repaint escape hatch |
| TTF metrics break hand-placed text layouts | Per-font metric tuning, overflow audit in the tours, bitmap fallback per font |
| 32-bit world rendering too slow | Profile in Phase 6 before content work; keep the 8-bit path; `Zh` cap by default |
| Licensing of derived art | Local generation (option 1) as the default; nothing derived in git |
| Volume of work (~7,500 files) | Usage-ordered delivery; the fallback makes partial packs fine; batch pipelines per class |
| Palette-swap and animation offsets break with HD sprites | Keep recolour masks; gates check offsets and frames |

## Suggested first PRs

1. Phase 0: `extract.py` + manifest + usage map from the e2e tours.
2. Phase 1: PNG/`@Nx` loader + a physical-resolution UI layer behind `hd_pack`, with a 3-asset
   test pack.
3. Phase 2: TTF backend for one font ID (`FONT10ARIAL`) end to end, then the rest.
