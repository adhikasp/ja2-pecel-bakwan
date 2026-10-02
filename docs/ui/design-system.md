# Design system: Night Ops

> **Status: Phase 1 output, for the owner to approve** (see *Done when* in
> [native-modern-game.md](../plan/native-modern-game.md)). Direction B, Night Ops, was chosen in
> [style-directions.md](style-directions.md). This page is the reference for every native screen: use these tokens and
> components. If a screen needs something new, add it here and to the gallery. Do not build one-off styles.

Everything lives in [`assets/ui/`](../../assets/ui/). It is copied next to the binary on every build and read at
runtime, so RML, RCSS and SVG edits need no recompile. Set `JA2_UI_DIR` to the source folder to skip the copy.

| File | What |
|---|---|
| `tokens.rcss` | Design tokens (colour, type, spacing, sizes, radii, elevation, motion) |
| `components.rcss` | Every component and its states, built only from tokens |
| `icons/*.svg` | The icon set: 109 icons on a 24-unit grid |
| `fonts/` | Barlow, Barlow Condensed, Share Tech Mono, Fira Sans (fallback). Noto Sans SC is downloaded at configure time |
| `gallery.rml`, `gallery.rcss` | The gallery screen |
| `mocks/` | The Night Ops mocks from the style-direction step (literal values, kept for reference) |

## Seeing it

- In the game: `ja2.debug("gallery", page, scale)`. The page is `controls`, `data`, `overlays`, `game`, `icons` or
  `tokens`, and the scale is `1`, `1.25`, `1.5` or `2`. The page tabs and Left/Right switch pages, and Esc returns.
  The toggle, slider, dropdown, table sorting, row selection and inventory drag and drop are live. Everything else
  shows a *forced* state (`.is-hover` and so on) next to the real one.
- Headless, without game data: `ja2-spike gallery <page> 1920x1080 1.5 out.png`. It also prints the layout audit.
- Tests:
  - Unit tests `UiSpike.Gallery*`, `IconsRasterize` and `TokensExpand`. They load every page at 1280x720, 1920x1080,
    2560x1440, 3440x1440 and 3840x2160 at 100–200% UI scale. They fail on any RmlUi warning (unknown property,
    token or icon) and on any layout-audit finding.
  - `tests/spike/gallery.lua` (ctest `spike_gallery`, label `spike-gamedata`) does the same in the game and
    screenshots every page.

**Scale.** Layout is in `dp` at a 1920×1080 reference. `1dp = min(w/1920, h/1080) × UI scale` physical pixels, so
at 100% the layout fills any 16:9 screen and wider screens get extra width. The gallery reads the scale from its
debug argument; Phase 2 will take it from the options screen.

**Layout audit** (`Screen::layoutProblems`, also `ja2.spike().problems`). No element may be off screen unless a
clipping container hides it, and nothing may overflow the page horizontally. At 100%, no page may need to scroll.
Above 100% the page scrolls vertically and its rows wrap. Every state cell has its own caption, so a wrapped row
still reads.

## Tokens

RCSS has no custom properties. `tokens.rcss` is therefore not a style sheet: it is a list of `--name: value;`. Every
`.rcss` the UI loads, and the gallery's RML, goes through a preprocessor (`ExpandTokens` in
`src/spike/RmlCommon.cc`). It replaces `var(--name)` with the value, and tokens may refer to other tokens. An
unknown token logs an RmlUi warning, which fails the tests. **Never hard-code a colour or size in a component.**

### Colour

| Group | Tokens |
|---|---|
| Surfaces (dark → light) | `--c-void #0B0F0D` screen · `--c-bg #0E1310` page · `--c-panel #121815` · `--c-panel-2 #1A221E` · `--c-panel-3 #232D28` · `--c-line #2A3630` · `--c-line-2 #3E4F46` · `--c-scrim #05080699` |
| Text | `--c-text-strong #EEF4EF` · `--c-text #D7E2D9` · `--c-text-dim #8FA096` · `--c-text-faint #5E6E65` (hints, disabled; large text only) · `--c-on-accent #0B0F0D` |
| Interaction | `--c-accent #9BE564` (phosphor: primary, on, active tab) · `--c-accent-2/-3` hover/pressed · `--c-accent-soft` wash · `--c-select #F2B33D` (amber: *the selected thing* — merc, sector, item) · `--c-focus #56B4E9` (keyboard focus only) |
| Status | `--c-ok #2BD4A0` · `--c-info #56B4E9` · `--c-warn #F0E442` · `--c-danger #FF6B3D`, each with a `-soft` wash |
| Game stats | `--c-stat-hp` (= danger) · `--c-stat-hp-lost` · `--c-stat-breath` (= info) · `--c-stat-morale` · `--c-stat-xp` (= select) · `--c-stat-track` |

**Colour-blind-safe status.** The status colours follow the Okabe–Ito set. Danger is vermilion rather than red, OK is
bluish green, and warning is yellow. They stay distinguishable with deuteranopia, protanopia and tritanopia, and
they also differ in lightness. The rules:
- A status is never shown by colour alone. It always comes with an icon (`ok`, `info`, `warning`, `error`) or a
  word.
- *Critical* also adds a pattern: the hatched bar fill (`.bar.critical`, a procedural `gen-hatch` texture).
- Focus (blue ring) and selection (amber) are separate from status, so "selected" never reads as "warning".

The brand green (`--c-accent`) is never used for status. OK is `--c-ok`, a different hue.

### Type

| Token | Value | Use |
|---|---|---|
| `--font-display` | Barlow Condensed 500/600/700 | Titles, labels, buttons, tabs (uppercase) |
| `--font-body` | Barlow 400/500/600/700 | Prose, lists, values |
| `--font-mono` | Share Tech Mono | Numbers, time, money, codes, key caps (tabular) |
| sizes | `--fs-2xs 12` · `xs 13` · `sm 15` · `md 17` (body) · `lg 20` · `xl 26` · `2xl 34` · `3xl 48` · `display 104` dp | |

**Languages.** The game ships English, German, French, Italian, Dutch, Polish, Russian and Simplified Chinese.
Barlow and Barlow Condensed cover Latin, including Polish (ą ć ę ł ń ó ś ź ż) and German/French. The gaps are filled
by two *fallback faces*, which RmlUi uses for any glyph the chosen family lacks:
1. **Fira Sans** (SIL OFL, committed): Cyrillic, Greek and Latin Extended. It covers Russian and any Latin glyph
   Barlow or Share Tech Mono miss.
2. **Noto Sans SC** (SIL OFL, 17 MB): Simplified Chinese. It is too large for the repository, so
   `src/spike/CMakeLists.txt` downloads it at configure time, pinned by google/fonts commit and SHA-256, into the
   build's `ui/fonts/notosanssc/` together with its `OFL.txt`. Offline builds show missing glyphs for Chinese and
   still build.

The tokens page shows one line in every shipped language, plus a Russian heading in the display style.

### Spacing, sizes, radii, elevation, motion

- **Spacing** (4dp grid): `--sp-1 4` · `2 8` · `3 12` · `4 16` · `5 24` · `6 32` · `7 48` · `8 64`.
- **Sizes:** `--h-control 36`, `--h-control-sm 28`, `--h-row 34`, `--icon-sm 16 / md 20 / lg 28`, `--slot 64`,
  `--border 1`, `--focus-ring 2`.
- **Radii** are squared, for a military look: `--r-0 0`, `--r-1 2` (controls, slots), `--r-2 4` (panels,
  popovers), `--r-round` (toggles, thumbs, badges).
- **Elevation:** a flat HUD with no drop shadows. Each level is a lighter surface plus a stronger border
  (`--e1-*` panel, `--e2-*` raised control, `--e3-*` menu/modal/tooltip). Modals add `--c-scrim`, and modals and
  menus get a 2dp accent top edge.
- **Motion:** `--t-fast 0.08s` (hover, press), `--t-base 0.15s` (toggle, tab, focus), `--t-slow 0.30s` (modal,
  toast, panel), easing `cubic-out` (`--ease-in cubic-in` for exits). Motion runs on the game's virtual clock, so
  headless runs are deterministic.

## Icons

There are 109 icons in `assets/ui/icons/`. They are our own work: 24×24 viewBox, 2-unit strokes, round caps and
joins, `currentColor`. They are drawn at load time by `RasterizeSvg` (`src/spike/IconRaster.cc`) at 96 px, or at
`icon-<name>@<px>`, with 4×4 anti-aliasing. The software renderer box-filters them when they are drawn smaller.
Use them as `<img class="icon" src="icon-<name>"/>` and tint them with `image-color`. The rasterizer covers the SVG
subset the set uses: path (all commands, including arcs), circle, ellipse, rect, line, polyline, polygon, fill and
stroke, dasharray, opacity, and rotate. Use only these features in new icons. `IconsRasterize` checks every file.

**The needs list.** It comes from the game code and from the Phase 0 manifest's *design-new* entries
(`interface/*`, `cursors/*`, 246 files):

| Group | Source in the game | Icons |
|---|---|---|
| Actions | cursors (`CUR_WALK/RUN/SWAT/PRON/LOOK/TALK/TARG/BST/TRW/PUNCH/STAB/WIRECUT/REPAIR/REMOTE/BOMB/EXIT/WAIT/KEY`), stance buttons (`STAND_ICON`, `CROUCH_ICON`), tactical/map bottom-bar buttons, message-box and list controls | end-turn, map, laptop, options, save, load, close, confirm, add, remove, search, filter, sort-asc/desc, chevrons, menu, more, drag, pause, play, fast-forward, stance-stand/crouch/prone, walk, run, sneak, look, talk, target, burst, throw, punch, stab, reload, climb, door, key, wire-cut, repair, remote, bomb, exit-sector, wait, inventory, trade |
| Status | soldier state (life, breath, morale, bleeding, drunk, sleep: `SLEEPICON`, collapsed, dead), contract, level-up, message-box icons (`MSGBOXICONS`) | health, breath, morale, action-points, bleeding, drunk, asleep, fatigue, suppressed, wounded, unconscious, dead, contract, level-up, ok, info, warning, error, lock |
| Assignments | `enum Assignments` (`src/game/Strategic/Assignments.h`): squads, ON_DUTY, DOCTOR, PATIENT, VEHICLE, IN_TRANSIT, REPAIR, TRAIN_SELF/TOWN/TEAMMATE/BY_OTHER, DEAD, POW, HOSPITAL | squad, on-duty, doctor, patient, vehicle, in-transit, repair, train-self, train-town, train-teammate, train-by-other, hospital, pow, dead |
| Item categories | item class flags (`IC_GUN, IC_LAUNCHER, IC_BLADE, IC_THROWING_KNIFE, IC_PUNCH, IC_GRENADE, IC_BOMB, IC_AMMO, IC_ARMOUR, IC_MEDKIT, IC_KIT, IC_FACE, IC_KEY, IC_MONEY, IC_MISC`), camouflage kit | gun, launcher, blade, throwing-knife, punch, grenade, bomb, ammo, armour, medkit, toolkit, face-gear, camouflage, key, money, misc-item |
| Map markers | map screen (`MILITIA`, `MINE_*`, `SAM`, `CHOPPER`/`HELICOP`, `PRISON`, `MERC_BETWEEN_SECTOR_ICONS`, `MERC_MVT_GREEN_ARROWS`, vehicles, sector inventory), towns, loyalty | town, sam-site, mine, militia, enemy, enemy-group, player-group, destination, waypoint, merc-moving, helicopter, airport, hospital, prison, vehicle, unexplored, loyalty, sector-inventory |

These are the needs so far, not every legacy image. The M1 audits of each screen (native-modern-game.md) will add
icons as screens are migrated.

## Components

Each component is a class in `components.rcss` with the markup below. States: every interactive component supports
*default, hover, active (pressed), focus, disabled, error*. Each is written as the real pseudo-class **and** an
`.is-*` class (`is-hover`, `is-active`, `is-focus`, `is-disabled`, `is-error`), so screens, the gallery and tests can
force a state. Precedence: disabled > error > active > focus > hover > default.

RmlUi does not lay out bare text inside a flex container, so **every label in a flex component is wrapped in a
`<span>`** (the markup below does this).

| Component | Markup | Variants and states |
|---|---|---|
| Button | `<button class="btn [btn-primary\|btn-ghost\|btn-danger]"><img class="icon" src="icon-map"/><span class="label">Map</span><span class="kbd">M</span></button>` | primary, secondary (default), ghost, danger × 6 states. Key hint in `.kbd` |
| Icon button | `<button class="icon-btn [on]"><img class="icon" src="icon-reload"/></button>` | `.on` for toggled tools (for example the stance) |
| Toggle | `<div class="toggle [on]"><span class="switch"><span class="knob"></span></span><span class="label">Ironman</span></div>` | off/on × 6 states |
| Slider | `<div class="slider"><div class="track"><div class="fill" style="width: 65%"></div></div><div class="thumb" style="left: 65%"></div><span class="value">65%</span></div>` | 6 states. The value is bound by the view model |
| Dropdown | `<div class="dropdown [open]"><div class="field"><span class="text">…</span><img class="icon sm" src="icon-chevron-down"/></div><div class="options"><div class="option [selected]">…</div></div><div class="error-text">…</div></div>` | closed/open × 6 states |
| Tabs | `<div class="tabs"><span class="tab [active]">Video</span>…</div>` | active, 6 states, badge |
| Badge | `<span class="badge [ok\|info\|warn\|danger]">2</span>` | |
| Table | `.table > .thead > .th[.sortable][.sorted]` (+ sort icon) and `.tr[.selected]` rows; `.num` for right-aligned mono numbers | sortable headers, row selected/hover/focus/error/disabled |
| List | `.list > .list-item[.selected]` with icon, label, `.meta` | 6 states + selected |
| Scroll area | `class="scroll"` (`overflow-y: auto`); scrollbars are styled globally | track, thumb hover/drag |
| Stat / progress bar | `<div class="stat"><span class="k">HP</span><span class="bar hp"><span class="fill" style="width:72%"></span><span class="lost" style="width:12%"></span></span><span class="v">72/88</span></div>` | hp, breath, morale, xp, ok, warn, generic progress, `.critical` (hatched), `.disabled`, `.thick` |
| Tooltip | `<div class="has-tip">…<div class="tooltip"><div class="tip-title">…</div>…<div class="tip-keys">…</div></div></div>` | shown on hover (`.show` forces it) |
| Modal | `.scrim` + `.modal[.danger] > .modal-head (.title) / .modal-body / .modal-foot (buttons)` | default, danger |
| Toast | `<div class="toast [ok\|warn\|danger]"><img class="icon" …/><div class="body"><div class="title">…</div><div class="text">…</div></div><img class="close" src="icon-close"/></div>` | info (default), ok, warn, danger. Always with a status icon |
| Context menu | `.menu > .menu-head / .menu-item[.danger] (icon, .label, .kbd or .chev) / .menu-sep` | hover, disabled, danger, submenu |
| Merc portrait card | `.merc-card[.selected\|.critical\|.asleep\|.dead]` > `.portrait (img, .ap)`, `.info (.name-row, 3 × .stat)`, `.footer` | default, hover, selected, focus, critical, asleep, dead |
| Item slot | `.slot[.selected\|.drop-ok\|.drop-bad\|.dragging]` > `.item (img, drag: clone)`, `.count`, `.cond > .fill`, or `.ghost` icon when empty | empty, filled, hover, selected, focus, drop ok/no, disabled, dragging |
| Inventory grid | `.inv` of `.slot`s. Drag and drop uses RmlUi's `dragstart` / `dragdrop` events on the slots | live in the gallery (`drag_start`, `drop`) |
| Time compression | `.timectl[.paused\|.is-disabled] > .clock (.day) + .tc-btn[.active]` (pause icon, 5m, 30m, 1h, 6h) | paused, running, hover, focus, disabled (combat) |

## Rendering notes

- UI images are generated. Icons are rasterized SVG, textures are procedural (`gen-grain`, `gen-hatch`, …), and
  merc faces are decoded from the player's game data at runtime (`face-<n>`). No game art is in the repository.
- On SDL's software renderer (headless screenshots, the game's frame buffer), the render interface rasterizes
  RmlUi geometry itself, because SDL's software `RenderGeometry` is not watertight. GPU renderers use
  `SDL_RenderGeometry`.
- RmlUi limits we design around: there are no custom properties (hence the token preprocessor), no box shadows or
  shaders on our render interface (hence flat elevation), gradients have two stops only, and bare text is not laid
  out in flex containers.

## Layout audit classes

`audit-skip` (decorative, not audited), `audit-over` (a floating layer: a menu or map overlay may cover what is under
it), `audit-pan` (pannable content, such as the zoomed strategic map, may be cut by its view on any side).

## Item art

Item pictures keep their shape: drawn at their own size times a whole number of output pixels (`item-<id>@<k>`,
k = 1..4), centred in the slot, never stretched to fill it (docs/ui/mapscreen.md).

## Known gaps

- The live gallery demonstrates keyboard focus with the forced `.is-focus` state. Real focus navigation (Tab,
  gamepad) comes with the Phase 2 runtime.
- A native `<input type="range">` / `<select>` is not used. The slider and dropdown are components bound to the
  view model. Text input is not in the Phase 1 list and has no component yet.
- Item pictures in slots are category icons. Real item art comes from the upscale pipeline (asset methodology).
- The 1280x720 frame buffer path is RGB565, which bands the gradients slightly. A GPU path would not.
