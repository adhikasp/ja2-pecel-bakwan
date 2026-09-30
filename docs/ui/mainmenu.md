# Main menu — functional spec (M1)

> The parity contract for the native main menu (see *Per-screen methodology* in
> [native-modern-game.md](../plan/native-modern-game.md)).

| | |
|---|---|
| Legacy code | `src/game/MainMenuScreen.cc` (`MainMenuScreenHandle`, `InitMainMenu`, `ExitMainMenu`, `HandleMainMenuInput`) |
| Screen id | `MAINMENU_SCREEN` |
| Reached from | start-up (after the splash), Options "Quit", credits, end of game, failed new game |
| Returns to | `GAME_INIT_OPTIONS_SCREEN`, `SAVE_LOAD_SCREEN`, `OPTIONS_SCREEN`, `CREDIT_SCREEN`, quit |
| ui_mode key | `mainmenu` (planned; default native at ≥1280x720) |
| Status | draft: wireframe (`assets/ui/mocks/phase3/mainmenu.rml`) awaiting the owner |

## 1. How it was audited

- `tests/e2e/phase3_audit.lua` (ui/text dumps and screenshots of every state below, 640x480).
- Code: all of `MainMenuScreen.cc`; `SaveLoadScreen.cc` (`gfLoadGameUponEntry`, the "load last save" path).
- Images: `loadscreens/mainmenubackground.sti` (backdrop), `loadscreens/ja2logo.sti`, `MLG_TITLETEXT` (the menu words as
  bitmap buttons). The native menu uses the backdrop at runtime as key art; the logo and the bitmap words are replaced by
  type.

## 2. Information shown

| # | Information | Source | Legacy | Native |
|---|---|---|---|---|
| I1 | Menu: New Game, Load Game, Preferences, Credits, Quit | fixed | bitmap buttons, centred | typeset list with index, a one-line hint and the key |
| I2 | Load Game unavailable | `AreThereAnySavedGameFiles()` | button disabled | item disabled, hint "No saves" |
| I3 | Version label | `g_version_label` | bottom left | footer |
| I4 | Copyright | `gzCopyrightText` | bottom centre | footer |
| I5 | Last save (new) | newest save header (`SaveGameInfo`): name, day/time, sector, mercs, balance, options | — | "Last save" card; hidden when there are no saves |
| I6 | Save count (new) | `GetValidSaveGames().size()` | — | hint on Load Game |

## 3. Actions

| # | Action | Legacy input | Game function | Native |
|---|---|---|---|---|
| A1 | New game | click New Game | exit to `GAME_INIT_OPTIONS_SCREEN` | `mainmenu.new`, key N (new) |
| A2 | Load game | click Load Game, key C | `gfSaveGame = FALSE`, `SAVE_LOAD_SCREEN` | `mainmenu.load`, keys L (new) and C |
| A3 | Load the last save at once | Alt+click Load Game, Alt+C | `gfLoadGameUponEntry = TRUE` | **Continue** item `mainmenu.continue`, key C? (see decisions); Alt+C kept |
| A4 | Options | click Preferences, key O | `guiPreviousOptionScreen = MAINMENU`, `OPTIONS_SCREEN` | `mainmenu.options`, O |
| A5 | Credits | click Credits, key S | `CREDIT_SCREEN` | `mainmenu.credits`, S |
| A6 | Quit | click Quit, Esc (key up), Ctrl+Q | `requestGameExit()` | `mainmenu.quit`, Esc, Ctrl+Q |
| A7 | Keyboard focus | — | — | Up/Down, Tab, Enter |

## 4. States

| # | State | Notes |
|---|---|---|
| S1 | splash / fade in | legacy splash and fade stay as they are; the native menu starts after `MainMenuIsReady()` |
| S2 | default | music `MUSIC_MAIN_MENU` |
| S3 | no saves | Load Game and Continue disabled, no card |
| S4 | window resized | `RelayoutMainMenu` — native relayouts by itself |

## 5. Popups
None (a message box can appear over it from other screens; the native one is used).

## 6. Sounds, animations
Button click sounds of the legacy buttons (keep one click sound); main menu music; splash fade.

## 7. Edge cases
- [ ] No saves (S3). [ ] Unreadable newest save: card hidden, Continue falls back to the next readable one.
- [ ] Newest save is Dead is Dead: Continue loads it like Alt+C does today.
- [ ] Long German/Russian menu words: the list is 640 dp wide; hints wrap away first.
- [ ] 640x480: legacy menu. [ ] 21:9: art covers, menu stays left.

## 8. Deliberately dropped or changed (proposed)

| Item | Decision | Reason |
|---|---|---|
| Bitmap title words and logo image | replaced by type | native text, localisable |
| Alt+click Load = load last save | surfaced as a Continue item (Alt+C kept) | discoverability |
| Last-save card, save count, key hints | added | modern UX |

## 9. Parity tour
`tests/e2e/mainmenu_parity.lua` (to write in M4): A1–A6 by id with screen assertions, S3 with an empty save dir, A3 loads
the newest save (`ja2.state().time`).
