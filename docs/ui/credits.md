# Credits — functional spec (M1)

> The parity contract for the native credits screen (see *Per-screen methodology* in
> [native-modern-game.md](../plan/native-modern-game.md)). Everything the legacy screen does is listed here; the native
> screen covers each row or says why not.

| | |
|---|---|
| Legacy code | `src/game/Credits.cc` (entry: `EnterCreditsScreen()`, handler: `HandleScreen<CREDIT_SCREEN>()`, exit: `ExitCreditScreen()`) |
| Native code | `src/game/NativeUI/CreditsScreen.cc` (view model `CreditsViewModel`, `credits`), `assets/ui/screens/credits.rml` + `.rcss` |
| Screen id | `CREDIT_SCREEN` |
| Reached from | main menu "Credits"; the end of the intro/ending video (`guiIntroExitScreen = CREDIT_SCREEN`) |
| Returns to | `MAINMENU_SCREEN` |
| ui_mode key | `credits` (default **native**; legacy below a 1280x720 output) |
| Status | parity-complete (Phase 2); design approved by the owner (wireframes in PR, `native-phase-2-wireframes/`) |

## 1. How it was audited

- Code read: all of `Credits.cc` (node list, code parser, eye blinking, face regions), `EDT.h` (`EDTFile::CREDITS`),
  the name tables (`gzCreditNames`, `gzCreditNameTitle`, `gzCreditNameFunny` in the language JSON).
- Automation: `ja2ctl ui --all`, `text` and screenshots of the legacy screen at 640x480 and 1920x1080
  (`tests/e2e/menus_tour.lua` keeps the legacy 640x480 golden).
- Images: `interface/credits.sti` (the 640x480 background with the team photo) and `interface/credit faces.sti` (three
  eye frames per person). The native screen cuts the fifteen faces out of the background at runtime; nothing derived
  from them is stored.

## 2. Information shown

| # | Information | Source | Legacy | Native | Notes |
|---|---|---|---|---|---|
| I1 | The credits text: headings, names, section gaps | `EDTFile::CREDITS` records from 1, `@` codes (`T` title, `{` `}` sections, `D` `B` `S` `J` `C` `R` spacing, speed, justification, colours) | text scrolling up the left strip, red headings | the reel on the right: headings in the accent colour, names below, a gap after each section | only T, `{`, `}` change the native layout; spacing/speed/colour codes are dropped (see §8) |
| I2 | The fifteen developers: name, role, joke line | `gzCreditNames/NameTitle/NameFunny[15]` | only for the face under the mouse, three lines at 375,420 | always on the roster cards (name, role); the detail panel adds the joke line for the hovered/focused person | three people have no joke line in the data (Camfield, Olsen, Brooks); legacy shows nothing for them either |
| I3 | Their faces | `credits.sti` background at the `gCreditFaces` rects | the team photo | a portrait per card, cut from the same photo | |
| I4 | Scroll position | time | text position | reel position; `offset` in the view model | |
| I5 | Paused | — | (never set) | "Paused" badge, Pause button turns into Resume | new |

## 3. Actions

| # | Action | Legacy input | Native input | Game function | Feedback |
|---|---|---|---|---|---|
| A1 | Leave | — | Back button (`credits.back`) | returns `MAINMENU_SCREEN` | |
| A2 | Leave | Esc (on key release) | Esc (on key release) | same | |
| A3 | See who someone is | hover a face | hover or focus a card (`credits.person[n]`), or click it | `CreditsViewModel::Select` | detail panel |
| A4 | Pause / resume the reel | — | Pause button (`credits.pause`), Space, P | `pause` command | badge, button label |
| A5 | Scroll by hand | — | mouse wheel over the screen, Up/Down, Page Up/Down, Home, End | `Scroll` | |
| A6 | Keyboard focus | — | Tab / Shift+Tab, arrows between cards, Enter/Space presses the focused control | RmlUi navigation | blue focus ring |

## 4. States and modes

| # | State | Entered by | Left by | Differences |
|---|---|---|---|---|
| S1 | running | entering the screen | Back, Esc, the end | the reel moves at 60 dp/s of game time (legacy: 1 px per 25 ms at 640x480) |
| S2 | finished | the last line leaves the top (or End) | — | returns to the main menu, like the legacy screen once its last node scrolled away |
| S3 | paused | Pause, Space, P | the same again | the reel stands still |
| S4 | compact | layout narrower than 1600 dp (big UI scale on a small output) | resize | roster in two columns |

## 5. Popups, overlays and modals

None. (The native cursor and toasts of the runtime are drawn over it.)

## 6. Sounds, animations and timing cues

| # | Cue | When | Asset | Must stay? | Native |
|---|---|---|---|---|---|
| C1 | Eye blinking | each face at its own interval (`sBlinkFreq`), eyes closed 150-300 ms | `credit faces.sti` | yes | the portrait shows the closed-eye frame over the face on the same timing (`credits-blink-<n>`) |
| C2 | Scrolling | continuous | — | yes | S1 |

The legacy screen plays no sound.

## 7. Edge cases

- [x] Empty: a credits file shorter than the window: the reel still runs through and ends.
- [x] Long translated strings: names and roles wrap on the cards; the layout audit fails on any text cut sideways.
- [x] 640x480: the native UI needs 1280x720, so the legacy screen runs (`credits_parity.lua` checks the fallback).
- [x] Widest windows and UI scale 100-200 %: screenshots at 1280x720 to 3840x2160, 21:9; the audit runs at 150 %.
- [x] Keyboard-only use: A2, A4-A6.
- [x] Missing game art: a card without a portrait keeps its frame; the text still shows.

## 8. Deliberately dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| Per-record spacing, speed, colour and justification codes | ignored | the design system sets type and spacing; all records are centred | owner (wireframes) |
| Names only on hover | names and roles always visible | modern UX: the roster is readable at a glance | owner (wireframes) |
| The team photo as a backdrop | portraits on cards | the photo is 640x480 art; the layout is native | owner (wireframes) |
| Nothing but Esc to leave | Back button, pause, manual scroll, focus | new conveniences, no rule changes | owner (wireframes) |

## 9. Parity tour

`tests/e2e/credits_parity.lua` (every resolution of `ctest -R resolution_`; goldens `credits_native.png`):

| Row | Covered by |
|---|---|
| S1 | `ja2.nativeUi().screen == "credits"` after clicking Credits |
| I1 | `viewModel("credits").lines` (> 50, starts with a heading) |
| I2, I3 | 15 people, a card per person by id, labels carry the names |
| A3 | hover `credits.person[1]`: selected, `credits.detail.funny` shows the joke |
| I4, S1 | the reel moves 50-70 dp per game second |
| A4, S3, I5 | click `credits.pause`: still for a second; label Resume; Space resumes |
| A5 | wheel and Up change the offset |
| A6 | Tab reaches `credits.pause`, Enter pauses |
| S4 | UI scale 150 %: `ja2.assertInsideScreen()` (layout audit) passes |
| A1 | click `credits.back` → main menu |
| A2 | Esc → main menu |
| S2 | End → the reel runs out → main menu |
| ui_mode | `ja2.setUiMode("credits", "legacy")` runs the legacy screen |
| < 1280x720 | the legacy screen runs |
