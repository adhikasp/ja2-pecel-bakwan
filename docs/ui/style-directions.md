# Style directions (Phase 1, step 1)

> **Status: for the owner to choose.** Phase 1 of [native-modern-game.md](../plan/native-modern-game.md) starts
> with 2–3 visual directions. This page proposes three. Each one is rendered **in the game, with RmlUi** (the Phase 0
> choice), on the same three mock screens. Tokens, components and the gallery come after a direction is picked.
> Pick one, or mix them (for example "A's type on B's layout").

All three keep JA2's identity: military, 1999, Arulco's tropics, and the typewriter/dossier look of AIM files and
merc contracts. They differ in how much of that they carry and where they put it.

| | A · Field Dossier | B · Night Ops | C · Arulco '99 |
|---|---|---|---|
| In one line | Manila folders and typed memos on an olive desk | A quiet modern tactical HUD, night-vision green | A late-90s travel poster of the island |
| Mood | Warm, tactile, nostalgic | Cool, precise, dense | Bright, friendly, bold |
| Closest to 1999 JA2 | Most | Least | Middle (the island, not the paperwork) |
| Readability at a glance | Good (dark ink on paper) | Best for numbers (mono on dark) | Good; the most colour to manage |
| Risk | Paper everywhere gets heavy on dense screens (inventory, laptop) | Generic "military shooter" if we are not careful | Can look like a different game; bright UI over the tactical world |

## How to see them

- In the game: `ja2.debug("styledemo", "a" | "b" | "c", "mainmenu" | "squadbar" | "mapscreen")`, for example
  `python tools/ja2ctl.py eval 'ja2.debug("styledemo", "b", "squadbar")'`. Esc returns. Merc faces are loaded from
  your own game data at runtime.
- Without the game or its data: `ja2-spike style b squadbar 1920x1080 out.png [hover-id]` (placeholder
  silhouettes instead of faces).
- The files: [`assets/ui-styles/`](../../assets/ui-styles/). The three RML documents (`mainmenu.rml`,
  `squadbar.rml`, `mapscreen.rml`) are shared. Each direction is one style sheet (`a.rcss`, `b.rcss`, `c.rcss`)
  over `base.rcss`. They are read from `ui-styles/` next to the binary at runtime, so edits need no rebuild (set
  `JA2_UI_STYLES` to point at the source folder while iterating).
- Screenshots: `tests/spike/style_demo.lua` (ctest `spike_style_demo`, label `spike-gamedata`) opens every
  direction × screen, checks the key elements are on screen, and saves `style_<dir>_<screen>_<W>x<H>.png`.

The same markup under three style sheets is deliberate. It shows the direction is a *skin* over one component
structure, which is how the design system will work. Placeholder data only; nothing on these screens is wired to
game state.

---

## A · Field Dossier

**Idea.** The UI is the paperwork of a mercenary operation. Menus are manila folders with tabs, data is typed on
paper, headings are stencilled, and warnings are red rubber stamps. Merc portraits are photos clipped to the file.
The dark olive desk underneath carries faint topographic lines.

**Palette**

| Token | Hex | Use |
|---|---|---|
| desk / desk-2 | `#23241C` / `#2E2F24` | Background, footer |
| manila / manila-2 | `#D8C391` / `#BFA56E` | Folders, tabs, bars' track |
| paper / rule | `#EEE5CC` / `#CBBC98` | Cards, memos, lines |
| ink / ink-2 | `#221E17` / `#5C5242` | Text, secondary text |
| stamp red | `#A3302A` | Primary action, selection, HP, danger |
| olive / khaki / steel | `#56602C` / `#8C8158` / `#3F5A6B` | Morale, labels, breath |
| status | ok `#4E7A34`, warn `#B9801A`, danger `#A3302A` | |

**Type.** *Stardos Stencil* (display: titles, tabs, big numbers) + *Courier Prime* (typed body and data) +
*Barlow Condensed* (small UI labels and key caps). All SIL OFL.

**Why.** It is the most recognisably JA2: the 1999 game already framed AIM, merc files and e-mails as documents.
Dark ink on warm paper reads well at every size. The stencil face gives the military note without camouflage
clichés.

**Watch out for.** A whole game of paper gets heavy on dense screens (inventory, laptop, item descriptions). The
design system would need a "clean" paper variant with less texture. Courier is wide, so tables need care. Stardos
Stencil is Latin-only, so Russian and Polish headings need a fallback.

## B · Night Ops

**Idea.** A modern tactical interface: near-black olive panels, hairline rules, condensed uppercase labels, and
monospaced readouts. One phosphor-green accent (night vision) and amber for "this is selected". The chrome stays
quiet so the data (HP, AP, money, time) reads first. The main menu tints merc photos like a night-vision feed.

**Palette**

| Token | Hex | Use |
|---|---|---|
| void / panel / panel-2 | `#0B0F0D` / `#121815` / `#1A221E` | Background and surfaces |
| line / line-2 | `#2A3630` / `#3E4F46` | Hairlines, borders |
| text / dim / faint | `#D7E2D9` / `#8FA096` / `#5E6E65` | Text levels |
| accent (phosphor) | `#9BE564` | Primary action, focus, healthy |
| select (amber) | `#F2B33D` | Selection, warnings |
| status | ok `#9BE564`, info `#5CC8E0`, warn `#F2B33D`, danger `#FF5A4E` | |

**Type.** *Barlow Condensed* (display, labels, buttons; uppercase) + *Barlow* (body) + *Share Tech Mono* (numbers,
codes, key caps). All SIL OFL.

**Why.** It is the most readable and scales best to dense screens. Tabular mono numbers line up, and the dark UI
sits calmly over the tactical world. It is the easiest to extend to every component.

**Watch out for.** Without care it becomes a generic military-shooter HUD and loses the 90s-Arulco character. It
would need the dossier/typewriter motifs in specific places (merc files, e-mail, AIM) to stay JA2. Green on black
has to be checked for colour-blind users next to the red and amber states. Barlow is Latin/Vietnamese only.

## C · Arulco '99

**Idea.** The island itself, as a late-90s travel poster or field guide. A sunset sky over deep jungle, warm sand
surfaces, a gold sun and hibiscus red for accents. Tall poster headlines, rounded pill buttons, and friendly type.
The strategic map becomes a brochure map: lagoon-blue sea and leaf-green land.

**Palette**

| Token | Hex | Use |
|---|---|---|
| jungle / jungle-2 / jungle-3 | `#0F2E28` / `#174238` / `#21564A` | Background and surfaces |
| leaf | `#6FA35A` | Secondary text, labels |
| sand / sand-2 | `#F3E3C3` / `#E4CFA5` | Text on dark, light surfaces |
| teal / lagoon | `#1F8A7A` / `#2BB3B1` | Player territory, breath, rules |
| hibiscus | `#E0443E` | Danger, HP, hover |
| sunset / gold / dusk | `#F28A2E` / `#F5C542` / `#3A1F3D` | Sky, primary and selected, shadows |
| status | ok `#6FA35A`, info `#2BB3B1`, warn `#F5C542`, danger `#E0443E` | |

**Type.** *Bebas Neue* (display: headlines, tabs, names) + *Fira Sans* (UI and body; covers Latin, Greek and
Cyrillic) + *Space Mono* (numbers). All SIL OFL.

**Why.** It has the most personality and is the most "tropical", which the old game had in its art but not in its
UI. It is friendly to new players, and the rounded controls read as modern.

**Watch out for.** It is the furthest from the military tone. Bright surfaces over the tactical world need care
(the squad bar is dark for that reason). Bebas Neue is capitals only and Latin only. There are many accent
colours, so the status colours must stay unambiguous.

---

## Common notes

- **Rendering.** Everything is RmlUi on SDL_Renderer (the software renderer headless). Only features our render
  interface supports are used: borders, radii, two-stop gradients, font effects, and images. Textures (paper grain,
  topographic lines, scanlines, hatching, grid, sun rays, vignette) are **procedural**, generated in
  `src/spike/RmlCommon.cc` (`gen-<name>` in RCSS). No art files are added. Real merc faces are read from the
  player's game data at runtime (`face-<n>`) and never saved. On SDL's software renderer (headless and the
  game's frame-buffer path) the render interface now rasterizes RmlUi's triangles itself: SDL's software
  `RenderGeometry` drew stray lines across rounded boxes. GPU renderers are unchanged. The game's RGB565 frame
  buffer shows some banding in C's sky gradient; a GPU path would not.
- **Scale.** Layout is in `dp` at a 1920×1080 reference; `dp = min(w/1920, h/1080)`. Screens were checked at
  1280×720, 1920×1080 and 3840×2160.
- **Fonts.** `assets/ui-styles/fonts/<family>/`, each with its `OFL.txt`. Taken from the Google Fonts repository
  (static TTFs). Language coverage is a known gap for A and B (Latin-only display faces). The font set is final
  only once tokens are defined; Fira Sans / Noto would be the fallback for Cyrillic and Polish diacritics are covered
  by all but Stardos Stencil.
- **Not in scope yet.** Tokens, components, icons and the gallery screen (Phase 1, after the choice). The mocks
  use hand-picked values per direction, not a token system.

| Font | Licence | Used by | Files |
|---|---|---|---|
| Stardos Stencil | SIL OFL 1.1 | A | Regular, Bold |
| Courier Prime | SIL OFL 1.1 | A | Regular, Bold |
| Barlow | SIL OFL 1.1 | B | Regular, Medium, SemiBold, Bold |
| Barlow Condensed | SIL OFL 1.1 | A, B | Medium, SemiBold, Bold |
| Share Tech Mono | SIL OFL 1.1 | B | Regular |
| Bebas Neue | SIL OFL 1.1 | C | Regular |
| Fira Sans | SIL OFL 1.1 | C | Regular, SemiBold, Bold |
| Space Mono | SIL OFL 1.1 | C | Regular, Bold |
