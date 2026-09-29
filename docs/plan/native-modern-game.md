# Native Modern Game — Rebuilding the Presentation for Modern Displays

> **Status: proposal.** This follows [graphics-remaster.md](graphics-remaster.md) (Phases 0–5,
> implemented). That plan kept the 1999 screens and made them fill a modern display. This plan
> **replaces** them. Gameplay rules, campaign data and save games stay as they are.
> The presentation (screens, HUD, art, text, world rendering) is rebuilt.

## Goal

JA2 should look and feel like a game made for a 1440p/4K monitor today, not a 640x480 game
blown up:

1. **Native layouts.** Every screen is redesigned for 16:9, 16:10 and 21:9 at a 1920x1080
   reference. It scales with DPI down to 1280x720 and up to 4K and 32:9. There is no centred
   640x480 box, no letterbox filler and no "classic coordinates".
2. **Native pixels.** Text is outline fonts. UI is vector or high-resolution art rendered at the
   physical resolution. Scaling can be fractional (125%, 150%, …), because nothing is
   pixel-doubled.
3. **Modern UX conventions,** where they don't change the rules: readable type, tooltips,
   hover states, scrolling lists, drag and drop, keyboard shortcuts shown in the UI, resizable
   panels, and consistent components across screens.
4. **Native world rendering.** Tactical and strategic maps are drawn by the GPU with HD tiles and
   sprites, smooth zoom, and lighting that matches today's look.
5. **Same game.** The same rules, numbers, campaign, saves and content data (`externalized/`
   JSON, maps, NPC scripts, speech).

**No longer goals:** pixel-identical 640x480 mode, hand-placed 640x480 coordinates, keeping old
art as the final look, or keeping the old screen code alive after migration. During the
migration, old and new screens coexist (see *Migration model*). The old path is deleted at the
end.

---

## Where things stand today (research)

### The code

- About 230k LOC across `src/game/Strategic`, `Tactical` and `Laptop`. Screens are
  **immediate-mode and hand-placed**. One `.cc` file per screen mixes game state, input
  (`MSYS_DefineRegion` regions and `QuickCreateButton` buttons at pixel coordinates) and drawing
  (`BltVideoObject` at pixel coordinates into `FRAME_BUFFER`). `MapScreen.cc` alone is 8k LOC,
  with about 11k more in `Map_Screen_Interface*.cc`.
- **Drawing** is software: 8-bit palettised STI → RGB565 through 42 blitters. The GPU only
  composites (remaster Phase 4 split the UI and world layers).
- **Text** is bitmap fonts (23 STIs in `Fonts.slf`), laid out in logical pixels.
- **Game state** is mostly global structs (`MercPtrs`, `gTacticalStatus`, `StrategicMap`,
  `gWorldItems`, …) plus functions that both change the state and trigger redraws.
- **Useful foundations:** the headless virtual clock, `ja2ctl` automation, golden screenshots,
  `ja2.debug(...)` hooks, runtime video settings, the layered compositor, and the VFS with mod
  layering.

### The art (JA2 Gold `Data/*.slf`)

| Archive | Images | What it is |
|---|---|---|
| `Tilesets.slf` | 4,978 STI + 60 PCX (+1,266 JSD structure files) | World tiles, structures, terrain |
| `Anims.slf` | 563 STI | Soldier, creature and vehicle animations |
| `Faces.slf` | 706 | Portraits, eye/mouth frames |
| `Radarmaps.slf` | 330 | Sector minimaps |
| `Laptop.slf` | 285 | Laptop OS and web pages |
| `Bigitems.slf` | 244 | Large item pictures |
| `Interface.slf` + `Data.slf` | ~270 | HUD, panels, buttons, icons, menus |
| `Cursors.slf` | 68 | Cursors |
| `Loadscreens.slf` | 46 | Loading art |
| `Fonts.slf` | 23 | Bitmap fonts |
| `Intro.slf` | 11 SMK | Videos |

The UI chrome art (~340 files) is **replaced by a new design system**, not upscaled. Content
art (faces, items, world) is **recreated at HD**. See *Asset methodology*.

---

## Target architecture

```
                 ┌──────────────── game logic (unchanged rules) ────────────────┐
                 │  globals: soldiers, items, sectors, time, AI, strategic AI…   │
                 └───────▲──────────────────────────────────────────┬────────────┘
                         │ actions (commands)                        │ state
                 ┌───────┴──────────── view models (new, per screen) ▼───────────┐
                 │  read-only projections + command functions, change events     │
                 └───────▲────────────────────────────────────────────┬──────────┘
                         │ data binding / events                       │
   ┌─────────────────────┴────────┐                         ┌──────────▼──────────────┐
   │  UI layer (new)              │                         │  World renderer (new)    │
   │  retained UI toolkit, layout │                         │  GPU tiles/sprites, HD   │
   │  in dp, TTF text, vector/HD  │                         │  atlases, zoom, lighting │
   │  art, themes, localisation   │                         │  shaders, picking        │
   └──────────────┬───────────────┘                         └──────────┬──────────────┘
                  └──────────── GPU compositor (SDL_GPU / SDL_Renderer) ┘
                                  physical resolution, any DPI
```

Key decisions (each one is confirmed by a spike in Phase 0):

1. **A retained UI toolkit with real layout.** The recommendation is **RmlUi** (MIT, C++, HTML/CSS-like
   RML/RCSS, flexbox, data binding, FreeType text, SDL backends, and used in shipped games).
   Screens become `.rml` documents plus `.rcss` styles plus a C++ view model. That gives
   resolution independence (dp units, flex layouts), theming, localisation-friendly text flow,
   and **moddable UI** as data. The alternative spike is a small in-house layout and widget layer
   on SDL_GPU. The decision is based on the spike results (perf, look, binding ergonomics,
   headless rendering).
2. **View models decouple UI from game logic.** Each migrated screen gets a view model that
   *reads* game globals into bindable data and exposes *commands* that call the existing
   game functions (assign merc, move squad, buy item, …). Game rules code is not rewritten. It
   is only split where it currently draws.
3. **The GPU renders everything.** UI via the toolkit's renderer. The world is rebuilt as a GPU
   sprite/tile renderer (texture atlases, depth sorting that reproduces JA2's Z rules, lighting and
   shade as shaders). The software blitters stay only until the world renderer replaces them.
4. **Units are dp (density-independent pixels).** The reference is 1920x1080 at 100%.
   `scale = min(physical_w / 1920, physical_h / 1080) × user_ui_scale`, fractional allowed, with
   a minimum layout size of 1280x720 dp. Layouts use flex and anchoring, not coordinates.
5. **Headless stays first-class.** The toolkit renders through a software or offscreen path in
   headless mode, so `ja2ctl shot`, goldens and the determinism checks keep working.
   Automation targets **element ids** (`ja2.click{id="mapscreen.merc[3].assignment"}`) instead of
   pixel positions.

---

## Migration model

A big-bang rewrite is too risky, so migration goes **screen by screen, behind a switch**:

- `ui_mode = legacy | native`, per screen, is resolved at `Enter*Screen`. A native screen fully
  owns input and drawing while it is active. Legacy screens keep working, drawn by the existing
  remaster path.
- Shared overlays (message boxes, tooltips, the cursor) are migrated first so that both kinds of
  screen can use them.
- Each screen ships when it reaches **functional parity** (see M2) and passes review. The
  default flips to `native` for that screen.
- When every screen is native, the legacy UI code and the old blitter-based UI path are deleted
  (the last phase).

---

## Per-screen methodology (used by every screen phase)

### M1. Audit what the screen does

- Use automation to dump every region, button and text element per state of the screen, and read
  the screen's code.
- Write a **functional spec**, `docs/ui/<screen>.md`: every piece of information shown, every
  action (mouse, keyboard, drag), every state and mode (e.g. map screen: plotting path, selecting
  destination, inventory open, contract popup…), every sound and animation cue, and edge cases
  (no mercs, vehicle, EPC, dead merc, 20 mercs).
- This checklist is the **parity contract**. Nothing on it gets lost silently. Anything
  deliberately dropped is written down with a reason.

### M2. Design

- **Wireframe first.** Low-fidelity layouts at 1920x1080, then checked at 1280x720, 2560x1440,
  3440x1440 and 3840x2160 (HTML mockups are fine). The layout rules say what grows, what scrolls
  and what docks where.
- **User approval gate.** Wireframes and one high-fidelity mock go to the project owner before
  implementation. Style is a product decision, not an agent decision.
- Everything comes from the **design system** (Phase 1): tokens, components and icons. A
  screen that needs a new component adds it to the system, not locally.

### M3. Build

- Extract the view model (unit-tested against game state fixtures), write the RML/RCSS, and wire
  commands to existing game functions.
- Keyboard shortcuts keep their current bindings. Add tooltips with shortcut hints.

### M4. Verify

- **Parity tour:** an e2e script walks the M1 checklist on the native screen by element ids,
  including game-state assertions (the merc really got assigned, the money really changed).
- **Legacy/native equivalence:** the same scripted actions on both paths produce the same game
  state (compare save-game dumps).
- **Layout audit** at every reference resolution and UI scale 100/125/150/200%: nothing clipped,
  overlapping, off-screen or truncated without an ellipsis or tooltip. This extends
  `ja2.assertInsideScreen()`.
- **Screenshot proof** per [AGENTS.md](../../AGENTS.md): each state of the screen at 1080p,
  1440p, 4K and 21:9. The reviewer opens them.

---

## Asset methodology (content art)

The UI chrome is designed new, not recreated. **Content** (faces, items, world, load screens,
radar maps, videos) is recreated at HD:

| Method | Used for |
|---|---|
| **Design new** (vector/procedural, from the design system) | All UI chrome, icons, cursors, map markers, buttons, frames |
| **Replace** | Fonts (open-licence TTF/OTF), system-like cursors |
| **Regenerate from data** | Radar maps and the strategic map, rendered from sector/map data at HD instead of upscaling images |
| **Upscale + cleanup** | Faces, item pictures, load screens, world tiles, animations: pixel-art-aware upscaler, alpha/edge fix, frame consistency, manual touch-up |
| **Hand repaint / new art** | The most-seen content where upscaling isn't good enough (merc portraits, key tilesets). Needs a human artist or curated generation. |

Tooling (Phase 0) supports all of these:
- `extract` (STI/PCX → PNG + JSON with frames, offsets, palettes)
- a per-screen **usage map**, to prioritise what matters
- a `manifest` with method, status and source per asset
- **quality gates:** exact scale multiples, silhouette/alpha match against the original, colour
  drift, frame-to-frame stability for animations, tile seams, size budgets
- **comparison sheets** for review

---

## Phases

| # | Phase | Result |
|---|---|---|
| 0 | Foundations and spikes | UI toolkit decided; asset tooling; render path chosen; per-screen audit template |
| 1 | Design system | Tokens, fonts, icon set, components, and a component gallery screen, approved by the owner |
| 2 | Native UI runtime | Toolkit integrated (GPU + headless), view-model framework, `ui_mode` switch, id-based automation, shared overlays (message box, tooltip, cursor, notifications) |
| 3 | Front-end screens | Main menu, options (incl. video/audio/controls), save/load, new-game setup, loading screens, credits |
| 4 | Strategic map screen | Redesigned full-screen strategic view with a GPU map and native sidebars, popups and inventory |
| 5 | Tactical HUD | Redesigned squad bar, inventory, item description, action menus, dialogue, messages, overhead map, placement |
| 6 | Laptop | Laptop OS + email, AIM, MERC, IMP, Bobby Ray, florist, insurance, files, finances, history, personnel, rebuilt natively (RML is a natural fit for "web pages") |
| 7 | Remaining screens | Pre-battle, auto-resolve, shopkeeper, sector inventory, merc hiring/contract flows, end-game; editor decision |
| 8 | Native world renderer | GPU tile/sprite renderer with HD atlases, smooth zoom, lighting/shadows as shaders, identical picking and Z rules |
| 9 | HD content art | Faces, item pictures, world tiles and animations, load screens, and optional video, per the asset methodology |
| 10 | Legacy removal | Delete legacy UI screens and the UI blitter path; simplify Video.cc |

Dependencies: 0 → 1 → 2, then 3–7 can proceed in parallel (one screen per PR stream). 8 depends
on 0 and 2 (for picking and overlay integration) and can start alongside 3. 9 follows 8 for world
art, but can start earlier for faces and items. 10 is last.

---

### Phase 0 — Foundations and spikes

- **UI toolkit spike.** Build the same small screen (e.g. the save/load list with a scrolling list,
  buttons, a modal and a tooltip) in RmlUi and in a minimal in-house layer on SDL_GPU. Compare
  perf at 4K, look, data-binding ergonomics, headless rendering, build and packaging on Windows,
  macOS and Android, licence and moddability. Write up the decision.
- **Render path spike.** SDL_GPU vs SDL_Renderer for the UI + world compositor. Headless offscreen
  rendering for screenshots.
- **World renderer spike.** Render one sector's static tiles on the GPU from the existing tile
  data, with the correct draw order against the software renderer's output (pixel diff).
- **Asset tooling.** Extract, usage map, manifest and quality-gate scaffolding (see the asset
  methodology).
- **Templates.** The functional-spec template (M1) and the screenshot/review checklist.

**Done when:** decisions are recorded in `docs/plan/native-modern-game-decisions.md`, the spikes
run in CI, and the inventory report lists every asset with its proposed method.

### Phase 1 — Design system

- **Visual direction:** mood board and 2–3 style directions for the owner to choose from. Keep
  JA2's identity (military, 90s-tropical, typewriter/dossier motifs) in a modern, readable form.
- **Tokens:** colour palette (plus colour-blind-safe status colours), type scale, spacing,
  radii, elevations/shadows, and motion timings.
- **Fonts:** open-licence faces for UI text, numbers (tabular), headings and a monospace/"typed
  document" face, with language coverage for every language the game ships.
- **Icon set:** a vector icon family for every action, status, assignment, item category and map
  marker used by the game (inventory of needs from the M1 audits and the Phase 0 usage map).
- **Components:** button, icon button, toggle, slider, dropdown, tabs, list/table with sorting,
  scroll area, tooltip, modal, toast/notification, progress/stat bar, merc portrait card, item
  slot (with drag and drop), inventory grid, context menu, and a time-compression control.
- **Gallery screen:** in-game (`ja2.debug("gallery")`) with every component in every state, at
  every reference resolution and scale. The owner approves the gallery screenshots.

**Done when:** the owner approves the gallery, and the tokens/components are documented in
`docs/ui/design-system.md`.

### Phase 2 — Native UI runtime

- Integrate the chosen toolkit: GPU renderer, headless renderer, input (mouse, keyboard, wheel,
  text input, gamepad later), DPI changes and runtime resolution changes.
- **View-model framework:** a binding helper, change notification from game events (hooks where
  the game currently sets "dirty" flags), commands, and test fixtures that load a save and assert
  on view-model output.
- **The `ui_mode` switch** per screen, and routing between native and legacy screens.
- **Automation:** `ja2ctl ui` lists native elements by id and label, and clicks and assertions work
  by id. Layout audit (clipping, overlap, off-screen, truncated text).
- **Shared overlays go native:** message box, tooltip, cursor, the screen-message queue as
  toasts, and fast-help.

**Done when:** a legacy screen can open a native message box. One trivial screen (credits) runs
native, with a parity tour, goldens and screenshot proof.

### Phase 3 — Front-end screens

Main menu (animated/key-art background), options (tabs: gameplay, video, audio, controls,
accessibility), save/load (thumbnails, metadata, sorting), new-game setup, loading screens (key
art + tips + progress), credits. Each one is its own PR, following M1–M4.

### Phase 4 — Strategic map screen

The largest UI redesign. Direction to validate in M2:
- A full-screen strategic map rendered by the GPU (Phase 8 renderer or a dedicated 2D map
  renderer), with pan and zoom, crisp sector grid, town/mine/SAM/airport overlays, routes, and
  militia/enemy/merc markers from the icon set.
- Dockable side panels: squad/merc list as a sortable table, merc details, and sector
  inventory as a grid with filters and drag and drop.
- A top/bottom bar: time and time compression, money and income, a message log that expands,
  and laptop/tactical buttons.
- Popups (assignment, contract, training, vehicles, repair) become context menus/modals from the
  design system.
- Parity is essential: the M1 checklist for this screen is long (plotting, helicopter, militia
  moves, contracts, sector inventory transfer, and so on).

### Phase 5 — Tactical HUD

- Squad bar (portrait cards with HP/AP/breath/morale and status icons), with a collapsible
  single-merc detail panel.
- Inventory and item description as a modal/side panel, with drag and drop and attachments.
- Action and talk menus, dialogue boxes/subtitles, the message log, turn/interrupt banners,
  overhead map (GPU minimap), and the tactical placement UI.
- Health bars, names and damage numbers drawn by the UI layer anchored to world positions
  (`WorldToScreen` from the Phase 8 renderer, or the Phase 4 layer transform until then).

### Phase 6 — Laptop

- Keep the diegetic "laptop" metaphor, now a native full-screen OS with windows sized for modern
  displays.
- Each website is rebuilt as RML/RCSS pages: AIM, MERC, IMP personality quiz, Bobby Ray shop,
  florist, insurance, funeral home, and the email/files/finances/history/personnel apps. Page
  content comes from the existing text data, not baked images.
- Content art (merc photos, product pictures) comes from Phase 9, and uses the upscaled originals
  until then.

### Phase 7 — Remaining screens

Pre-battle, auto-resolve (a readable battle panel), shopkeeper (trade UI with a grid and
comparisons), merc hiring and contract flows, end-game screens, and the intro/ending
presentation. **Editor decision:** keep the legacy editor or rebuild it later; it is not
player-facing.

### Phase 8 — Native world renderer

- A GPU renderer for tactical sectors: texture atlases from tiles and animations, a draw order
  that reproduces JA2's layering (land, objects, structures, shadows, mercs, roofs, onroof,
  topmost) and height offsets, and **per-pixel equivalence tests** against the software renderer
  at 1x before any HD art.
- Lighting and shade tables as shaders; translucency, outlines and highlights; night vision and
  explosions.
- Smooth (fractional) zoom and camera, with edge scrolling and keyboard panning.
- Picking and interaction use the same tile/structure data, with automated equivalence tests
  (click → same grid number as the legacy path).
- Performance target: 4K at 60 fps on integrated graphics (this dev machine class).

### Phase 9 — HD content art

Per the asset methodology, in usage order:
1. Merc faces (the most-seen content). Upscale + cleanup, with Hand repaint for the most-seen
   mercs.
2. Item pictures (small and big).
3. World tiles by tileset, then animations. Palette-swap body parts (hair, skin, vest, pants)
   keep working through recolour masks. Tile seams and animation frame stability are gated.
4. Load screens, radar maps (regenerated from data) and videos (optional upscaling).

Each batch is its own PR with comparison sheets and gate reports.

### Phase 10 — Legacy removal

Delete the legacy UI screen code, the 640x480 layout helpers (`STD_SCREEN_*`, anchor shims), the
UI blitter path, and golden images of the legacy screens. Keep the software world renderer only
if it is still needed as a fallback (decide then).

---

## Licensing and distribution

The original art belongs to the JA2 rights holders. Stracciatella ships no game data.
- **In the repo:** engine code, tools, the design system, open-licence fonts, and **newly created**
  UI art and icons (our own work).
- **Derived content** (upscaled or repainted originals) is **not committed**. The default is to
  generate it on the player's machine from their own game files with a deterministic pipeline.
  The alternative is a separately distributed pack. This needs deciding before Phase 9.

## Cross-cutting concerns

- **Saves and gameplay:** no change. The view models only read and call existing functions. The
  equivalence tests (M4) guard this.
- **Mods:** `externalized/` data keeps working. UI mods become RML/RCSS overrides through the VFS.
  Old pixel-coordinate UI JSON settings become obsolete; list which ones in Phase 2.
- **Localisation:** all UI text goes through the existing string tables. Layouts must survive long
  German/Russian strings; the layout audit runs per language.
- **Accessibility:** UI scale, colour-blind-safe palette, font size, remappable keys, and optional
  reduced motion.
- **Platforms:** Windows, macOS (Retina) and Linux; Android keeps working through the same UI
  with touch-sized targets (a later concern).
- **Performance budget:** UI ≤ 2 ms per frame at 4K; world rendering per Phase 8.

## Main risks

| Risk | Mitigation |
|---|---|
| Losing features in the redesign | M1 functional spec as a parity contract; parity tours; legacy/native state equivalence |
| Game logic tangled with drawing | View models call existing functions; split drawing out minimally; the per-screen switch allows gradual work |
| Toolkit choice doesn't hold up | Phase 0 spike with a real screen; the view-model layer keeps the toolkit replaceable |
| Style disagreement / churn | Owner approval gates at design-system and per-screen wireframe stage |
| World renderer order/lighting differences | Pixel-equivalence tests against the software renderer at 1x before HD art |
| Art volume (~7,500 files) | Design-new for UI (~340 files eliminated); usage-ordered content batches; pipeline + gates |
| Licensing of derived art | Local generation by default; nothing derived in git |

## Suggested first PRs

1. Phase 0: UI toolkit spike (save/load list in RmlUi and in-house) + decision doc.
2. Phase 0: asset tooling (extract + usage map + manifest).
3. Phase 0: world renderer spike (one sector, pixel diff vs the software renderer).
4. Phase 1: style directions (mock screenshots of the main menu, squad bar and map screen in 2–3
   directions) for the owner to choose from.
