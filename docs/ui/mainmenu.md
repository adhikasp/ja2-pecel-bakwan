# Main menu — functional spec (M1)

> The parity contract for the native main menu (see *Per-screen methodology* in
> [native-modern-game.md](../plan/native-modern-game.md)).

| | |
|---|---|
| Legacy code | `src/game/MainMenuScreen.cc` (`MainMenuScreenHandle`, `InitMainMenu`, `ExitMainMenu`, `HandleMainMenuInput`) |
| Screen id | `MAINMENU_SCREEN` |
| Reached from | start-up (after the splash), Options "Quit", credits, end of game, failed new game |
| Returns to | `GAME_INIT_OPTIONS_SCREEN`, `SAVE_LOAD_SCREEN`, `OPTIONS_SCREEN`, `CREDIT_SCREEN`, quit |
| Native code | `src/game/NativeUI/FrontMainMenu.cc` (`MainMenuViewModel`, "mainmenu"), `assets/ui/screens/mainmenu.rml`, `frontend.rcss` |
| ui_mode key | `mainmenu` (default native; legacy below a 1280x720 output) |
| Status | parity-complete (Phase 3); design approved by the owner (wireframes `native-phase-3-wireframes/`) |

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
| A2 | Load game | click Load Game, key C | `gfSaveGame = FALSE`, `SAVE_LOAD_SCREEN` | `mainmenu.load`, key L (C is Continue now, owner decision) |
| A3 | Load the last save at once | Alt+click Load Game, Alt+C | `gfLoadGameUponEntry = TRUE` | **Continue** item `mainmenu.continue`, keys C and Alt+C (the newest save by file time) |
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

## 8. Deliberately dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| Bitmap title words and logo image | replaced by type | native text, localisable | owner |
| Alt+click Load = load last save | surfaced as a Continue item; C = Continue, Alt+C kept, L = Load Game | discoverability | owner |
| Last-save card, save count, key hints | added | modern UX | owner |
| Esc quits without asking | kept (as legacy) | parity | owner |
| Background | the original `mainmenubackground.sti`, aspect kept, covering the screen and cropped, uplifted 4x with Scale2x at runtime (nothing derived is stored); dark gradient behind the menu, which sits on its own panel | owner: render the original art faithfully, readable hints | owner |

## 9. Parity tour

`tests/e2e/mainmenu_parity.lua` (every resolution of `ctest -R resolution_`; golden `mainmenu_native.png`):

| Row | Covered by |
|---|---|
| S2, I1 | `ja2.nativeUi().screen == "mainmenu"`, every item by id |
| I3, I4 | `viewModel("mainmenu").version/copyright` |
| S3, I2 | fresh home: no card, Continue and L do nothing |
| A4 | options by id and by the legacy label "Preferences", Esc back |
| A5 | S opens the credits |
| A1 | N and the item open the new-game screen, Cancel back |
| A7 | Tab focuses a menu item |
| layout | UI scale 150 % and 200 %: `ja2.assertInsideScreen()` |
| ui_mode | `ja2.setUiMode("mainmenu", "legacy")` runs the legacy menu |
| < 1280x720 | the legacy menu runs |
| A3, I5, I6 | `saveload_parity.lua`: the last-save card (golden `mainmenu_lastsave.png`), C loads the newest save (map screen, sector A9) |
