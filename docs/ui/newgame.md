# New game setup (GIO) — functional spec (M1)

| | |
|---|---|
| Legacy code | `src/game/GameInitOptionsScreen.cc` (`GameInitOptionsScreenHandle`, `EnterGIOScreen`, `DoneFadeOutForExitGameInitOptionScreen`) |
| Screen id | `GAME_INIT_OPTIONS_SCREEN` |
| Reached from | main menu New Game |
| Returns to | `MAINMENU_SCREEN` (cancel), `INTRO_SCREEN`, or `SAVE_LOAD_SCREEN` (Dead is Dead) |
| ui_mode key | `newgame` (planned) |
| Status | draft: wireframe `assets/ui/mocks/phase3/newgame.rml` |

## 2. Information and choices

| # | Group | Options | Stored in |
|---|---|---|---|
| I1 | Difficulty Level | Novice, Experienced, Expert | `gGameOptions.ubDifficultyLevel` |
| I2 | Extra Difficulty | Save Anytime, Iron Man "(Cannot save during combat)", Dead is Dead "(Cannot load previous savegames)" | `ubGameSaveMode` |
| I3 | Game Style | Realistic, Sci Fi | `fSciFi` |
| I4 | Gun Options | Normal, Tons of Guns | `fGunNut` |
| — | Timed turns | compiled out in JA2 Gold (`#if 0`) | not shown |

Defaults come from the last `gGameOptions`.

## 3. Actions

| # | Action | Legacy | Game function |
|---|---|---|---|
| A1 | Pick an option | click radio box | radio group |
| A2 | Ok | button, Enter | Iron Man / Dead is Dead warning (`str_iron_man_mode_warning`, `str_dead_is_dead_mode_warning`) → difficulty confirm (`zGioDifConfirmText`) → Dead is Dead: name prompt → save screen; else fade → intro |
| A3 | Cancel | button, Esc | main menu |

## 5. Popups
The three yes/no boxes and the Dead is Dead ok box, native message box. "No" on the save-mode warning resets to Save Anytime.

## 6. Cues
Radio click sound, fade out, music stops on exit.

## 8. Changed (proposed)

| Item | Decision |
|---|---|
| Radio check boxes | option cards with a one-line description (descriptions for Save Anytime, styles and guns are **new strings**) |
| Summary panel | new, shows the choice and that it is fixed for the campaign |
| Keyboard | Tab between groups, Left/Right inside (new) |

## 9. Parity tour
`newgame_parity.lua`: each group sets `gGameOptions`; warning chain; Cancel; Dead is Dead reaches the save screen.
