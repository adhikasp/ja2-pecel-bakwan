# New game setup (GIO) — functional spec (M1)

| | |
|---|---|
| Legacy code | `src/game/GameInitOptionsScreen.cc` (`GameInitOptionsScreenHandle`, `EnterGIOScreen`, `DoneFadeOutForExitGameInitOptionScreen`) |
| Screen id | `GAME_INIT_OPTIONS_SCREEN` |
| Reached from | main menu New Game |
| Returns to | `MAINMENU_SCREEN` (cancel), `INTRO_SCREEN`, or `SAVE_LOAD_SCREEN` (Dead is Dead) |
| Native code | `src/game/NativeUI/FrontNewGame.cc` (`NewGameViewModel`, "newgame"), `assets/ui/screens/newgame.rml` |
| ui_mode key | `newgame` (default native) |
| Status | parity-complete (Phase 3); design and the new descriptions approved by the owner |

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

## 8. Changed

| Item | Decision |
|---|---|
| Radio check boxes | option cards with a one-line description (descriptions for Save Anytime, styles and guns are **new strings**) |
| Summary panel | new, shows the choice and that it is fixed for the campaign |
| Keyboard | Tab and arrows between the cards; Enter starts on release (legacy Enter skipped the confirmations; the native one asks like the button) |
| Fade out | not reproduced; the intro (or the save screen for Dead is Dead) follows at once |

## 9. Parity tour

`tests/e2e/newgame_parity.lua` (golden `newgame_native.png`): every choice by id and the summary; the Iron Man warning,
No resets to Save Anytime; Esc cancels; a real start (warning, difficulty confirmation, laptop), and the started game has
the chosen settings (`ja2.viewModel("options")` reads `gGameOptions`); layout audit at 150 %. Not in the tour: the Dead
is Dead save-name step.
