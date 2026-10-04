# Victory epilogue (end game) - functional spec (M1)

The screen the ending cinematic hands over to: the campaign's last page before the credits. **There is no
legacy screen for this** — the old chain went straight from the ending videos to the credits — so this spec is
the definition of a new screen (the M1 "audit" is the audit of the *chain* it belongs to and of the game state
it reports), and §9 is its verification map.

| | |
|---|---|
| Legacy code | none. The chain it extends: `src/game/Intro.cc` `PrepareToExitIntroScreen()` (`INTRO_ENDING` used to go to `CREDIT_SCREEN`) |
| Screen id | `EPILOGUE_SCREEN` (what `ja2.screen()` returns) |
| Reached from | the end of the ending cinematic (`INTRO_SCREEN`, mode `INTRO_ENDING`, played or skipped); `ja2.debug("epilogue")` in tests |
| Returns to | `CREDIT_SCREEN` ("Continue to credits") · `MAINMENU_SCREEN` ("Main menu"); both after `ReStartingGame()` |
| Owner of this spec | adhikasp — scope chosen 2026-10-04 ("Cinema + victory epilogue") |
| Status | reviewed |

## 1. How it was audited

- **The chain:** `Overhead.cc` (the Queen's death) → `End_Game.cc` (the tactical end sequence: the killer's and
  witnesses' quotes, the move to P3, the NPC script gate, `QUOTE_END_GAME_COMMENT` from every survivor) →
  `DoneFadeOutEndCinematic()` → `INTRO_SCREEN` (`INTRO_ENDING`) → (this screen) → `ReStartingGame()` → credits.
  The old chain called `ReStartingGame()` from `Intro.cc` before the credits; it now happens when this screen
  leaves, because the screen reads the campaign state it is about to reset.
- **The state it reports** (all read once, at entry, into the view model):
  | Stat | Source |
  |---|---|
  | Days in Arulco | `GetWorldDay()` (`src/game/Strategic/Game_Clock.h`) |
  | Sectors liberated | the surface sectors with `!StrategicMap[i].fEnemyControlled` (`Campaign_Types.h`) |
  | Enemies killed | `gStrategicStatus.usEnemiesKilled[ENEMY_KILLED_TOTAL][ADMIN/TROOP/ELITE]` (`Strategic_Status.h`), with the rank breakdown as the subline |
  | War effort | `CurrentPlayerProgressPercentage()` (`src/game/Tactical/Campaign.cc`), 0–100 |
  | Those who served | `gMercProfiles` with `ubMiscFlags & PROFILE_MISC_FLAG_RECRUITED` |
  | The fallen | the same, with `bMercStatus == MERC_IS_DEAD` |
- **No popups, no sound of its own.** The chain around it (the tactical end sequence, the videos) is unchanged.

## 2. Information shown

| # | Information | Source | Shown as | Shown when | Notes |
|---|---|---|---|---|---|
| I1 | "Arulco is free" | `Str("epilogue.title")` | display type, accent | always | the game's own words for this moment (`PeopleContent.cc`, the kill-Deidranna quest) |
| I2 | The one-line ending | `Str("epilogue.sub")` | body, dim | always | |
| I3 | The epilogue paragraph | `Str("epilogue.text")` | body, max 60 dp wide column | always | curated, 2–3 sentences |
| I4 | Days in Arulco | `GetWorldDay()` | stat card | always | "day 1" on a fresh state |
| I5 | Sectors liberated | the count of player-controlled surface sectors | stat card | always | |
| I6 | Enemies killed | `usEnemiesKilled[ENEMY_KILLED_TOTAL][*]` | stat card + "N admins · N troops · N elites" | always | |
| I7 | War effort | `CurrentPlayerProgressPercentage()` | a progress bar with the percentage | always | the game's own 0–100 estimate |
| I8 | Those who served | recruited profiles | portrait chips (name + portrait), "N mercs" | when any | |
| I9 | The fallen | recruited profiles with `bMercStatus == MERC_IS_DEAD` | portrait chips, dimmed with the `dead` icon and a "fallen" badge | when any | a memoriam row; hidden when nobody fell |
| I10 | Buttons | `Str("epilogue.continue")`, `Str("epilogue.menu")` | primary + ghost button with key hints (`Enter` / `Esc`) | always | |

## 3. Actions

| # | Action | Input | Game function | Preconditions / disabled when | Feedback |
|---|---|---|---|---|---|
| A1 | Continue to the credits | click "Continue to credits" / `Enter` | `ReStartingGame()` → `CREDIT_SCREEN` | always (default button) | the credits screen |
| A2 | Go to the main menu | click "Main menu" / `Esc` | `ReStartingGame()` → `MAINMENU_SCREEN` | always | the main menu |
| A3 | Read a name in full | hover/focus a chip (I8, I9) | — | — | the native tooltip (`title`) with the full name and the fate |

## 4. States and modes

| # | State | Entered by | Left by | Differences |
|---|---|---|---|---|
| S1 | won, survivors and fallen | the end of the ending chain | A1, A2 | the full screen (I1–I10) |
| S2 | won, nobody fell | the same | A1, A2 | I9's row is hidden |
| S3 | no campaign behind it (`ja2.debug("epilogue")`, or a chain that somehow skipped the war) | debug hook | A1, A2 | the stats show the fresh-state values (day 1, 0 sectors, 0 kills); nothing is missing or empty |

## 5. Popups, overlays and modals

None. The native tooltip (A3) is the only thing that appears over the screen.

## 6. Native design (M2)

Wireframes: `assets/ui/mocks/phase7/epilogue.rml`, `epilogue_fallen.rml` (`tools/ui/phase7_mocks.py`), shown in
game with `ja2.debug("mock", "phase7/epilogue")`.

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│                        A R U L C O   I S   F R E E                       │
│                  Deidranna is dead. The war is over.                      │
│                                                                          │
│   ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐                   │
│   │    18    │ │    41    │ │   612    │ │     9    │                   │
│   │   days   │ │ sectors  │ │ enemies  │ │  mercs   │                   │
│   │          │ │          │ │ 21a·402t·189e │       │                   │
│   └──────────┘ └──────────┘ └──────────┘ └──────────┘                   │
│   war effort  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░  78%                            │
│                                                                          │
│   Those who came home                                                    │
│   (face) Fox   (face) Len   (face) Ivan   (face) Barry                   │
│   The fallen                                                             │
│   (face) Trevor †   (face) Kyle †                                        │
│                                                                          │
│           [ Continue to credits  ⏎ ]      [ Main menu   Esc ]           │
└──────────────────────────────────────────────────────────────────────────┘
```

- **One centred column** (1360 dp) on the dark stage (`--c-void` with the `gen-mottle` texture at low
  opacity). The heading block, then a row of four stat cards: big number in `--font-display`, label in
  `--fs-2xs` caps, the detail line as the card's subline. Then the war-effort bar full width, then the two
  roster rows, then the actions.
- **The roster** is portrait chips: portrait, short name; the fallen are dimmed with the `dead` icon and a
  `danger`-soft "fallen" badge. Rows wrap; below 1500 dp the chips shrink (`compact` class).
- **Actions** are one primary ("Continue to credits", `Enter`) and one ghost ("Main menu", `Esc`), with `.kbd`
  hints. Nothing auto-advances: the player reads at their own pace (reduced-motion friendly by construction).
- **Everything is data.** The screen is the view model (`EpilogueViewModel`) plus this layout: every number is
  read once at entry, so the page cannot change under the reader.

## 7. Edge cases

- [x] **The epilogue with no campaign behind it** (S3): the four cards read the fresh-state numbers (day 1, 0
      sectors, 0 kills, nobody served) — nothing is hidden or missing.
- [x] **The epilogue with nobody fallen** (S2): the "the fallen" row is not drawn at all.
- [x] **1280x720** (the native floor): the column is capped at 100% and everything fits; the layout audit passes.
- [x] **UI scale 125% / 150% / 200%**: below 1500 dp the page switches to its compact density (smaller heading,
      tighter cards and chips) so the story *and* both buttons fit without scrolling; the page scrolls when even that
      is not enough (the scroll container centres with auto margins, so nothing is unreachable above the origin —
      the trap a `justify-content: center` column has).
- [x] **3440x1440 (21:9) and 2560x1080 (ultrawide)**: the column stays centred at 1360 dp; the extra width is
      margin, so the page reads the same at every width.
- [x] **Long strings**: the paragraph is capped at 640 dp and wraps; the stat cards' detail line wraps inside the
      card rather than being cut. German falls back to English for the keys this screen introduced
      (`assets/ui/strings/strings-eng.json`), as the loading-screen tips do.
- [x] **Keyboard-only**: `Enter` continues to the credits, `Esc` to the main menu; both are shown as `.kbd` hints,
      and Tab reaches the buttons in order.
- [x] **A merc with no bigface**: the portrait falls back to the legacy `bNN.sti` face (the `face-` provider in
      `NativeImages.cc`).
- [x] **A merc with no nickname**: the full name is used.

## 8. Wiring, sounds and timing

- **Wiring.** `ui_mode` key `epilogue` (default native), route `{ EPILOGUE_SCREEN, "epilogue", &CreateEpilogueScreen }`.
  `Intro.cc`'s `INTRO_ENDING` exit now returns `EPILOGUE_SCREEN`; the epilogue calls `ReStartingGame()` (the
  same call the intro screen used to make) on the way out, so both UIs keep one chain. Below 1280x720 or
  without RmlUi there is no native screen to show: the legacy handler does exactly what the old chain did
  (`ReStartingGame()` → `CREDIT_SCREEN`) — the epilogue is new content, so nothing is lost at that size.
- **Sounds and timing.** Silent, like the credits screen. The music the end sequence set (victory) keeps
  playing until `ReStartingGame()` stops all sound. No timers.
- **Deterministic surface.** `ja2.debug("epilogue")` opens the screen over any state (S3 with a fresh game);
  `ja2.viewModel("epilogue")` reports `days`, `sectors`, `killed`, `served`, `fell`, `effort`, the `stats` cards,
  the `survivors` and `fallen` rows and the labels; the buttons are `epilogue.continue` / `epilogue.menu`.

## 9. Deliberately dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| The old chain: ending videos → credits | a victory epilogue in between | the slice is "end-game screens"; this is the end-game screen | adhikasp, 2026-10-04 ("Cinema + victory epilogue") |
| `ReStartingGame()` called by `Intro.cc` | called by this screen when it leaves | the screen reads the campaign state it would otherwise wipe | this PR |
| A defeat / game-over screen | not built | JA2 has no lose condition today; inventing one is a design job of its own (the revamp's deed ledger) | adhikasp, 2026-10-04 (option not taken) |
| Money, militia, reputation, man-days | not shown | the four stats and the roster are the story of the war; more numbers belong to the laptop's history page | this PR (curate) |
| Below 1280x720 | the epilogue is skipped (legacy handler → credits) | no native UI below that size; parity with the old chain | — |

## 10. Parity tour

`tests/e2e/epilogue_parity.lua` (rows: the checklist above).

| Row | Covered by (step / assertion) |
|---|---|
| S3 | `ja2.debug("epilogue")` right after the new game: the four cards, no kills, nobody served, no fallen row |
| S2, I1–I8 | a campaign staged with `ja2.debug("campaign", …)` (day 18, three liberated sectors, 612 kills, two mercs): the heading, the subline, the paragraph, all four cards with their sublines, the effort bar and the home row |
| S1, I9 | a third merc staged `dead = true`: the fallen row appears, "of them 1 fell", and his fate reads "fell in Arulco" |
| A3 | hover the fallen chip: the tooltip shows the name and the fate (screenshot) |
| — | the chain: `ja2.debug("intro", "ending", "still")` → `skipall` → the epilogue (the screens hand over to each other); and the chain's last scene playing out → the epilogue on its own |
| A2 | `Esc` → `MAINMENU_SCREEN` |
| A1 | "Continue to credits" → `CREDIT_SCREEN`, and the credits are the native screen |
| §7 | layout audit (`ja2.assertInsideScreen()`) plus the Continue button on screen at UI scale 150% |
| — | resolution matrix: `ctest -L resolution` (1080p) and the full `ctest -R resolution_`; goldens at 1280x720, 1920x1080, 2560x1080 and 3440x1440 |
