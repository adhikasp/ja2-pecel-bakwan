# Save / Load — functional spec (M1)

| | |
|---|---|
| Legacy code | `src/game/SaveLoadScreen.cc` (`SaveLoadScreenHandle`, `EnterSaveLoadScreen`, `GetSaveLoadScreenUserInput`, `SaveLoadSelectedSave`, `DisplaySaveGameEntry`) |
| Screen id | `SAVE_LOAD_SCREEN` (mode `gfSaveGame`) |
| Reached from | main menu Load / C / Alt+C; options Save/Load; new game (Dead is Dead asks for a name) |
| Returns to | `guiPreviousOptionScreen`; after loading, the saved screen |
| Native code | `src/game/NativeUI/FrontSaveLoad.cc` (`SaveLoadViewModel`, "saveload"), `assets/ui/screens/saveload.rml`; what it shares with the legacy screen is in `SaveLoadScreen.cc` (`SaveLoadNative*`) |
| ui_mode key | `saveload` (default native) |
| Status | parity-complete (Phase 3); design approved by the owner |

## 1. Audit
`phase3_audit.lua` states `save`, `load`, `load_delete_confirm`. Images: `loadscreen.sti`, `save-load-addons.sti` (skull
icon), `scroll-bar.sti`, `loadscreenaddons.sti`: replaced.

## 2. Information

| # | Information | Source | Legacy | Native |
|---|---|---|---|---|
| I1 | Save list, newest first | `GetValidSaveGames`, sorted by file time (`compareSaveGames`) | 11 rows + scrollbar | scrolling table, sortable columns |
| I2 | Description | `header.sSavedGameDesc` | row | Name column + file name |
| I3 | Game time | `uiDay`, `ubHour`, `ubMin` | "Day N, hh:mm" | column |
| I4 | Sector | `GetSectorIDString(sSector)`; "N/A" before start; `STR_LATE_14` otherwise | column | column |
| I5 | Mercs | `ubNumOfMercsOnPlayersTeam` | "N Mercs" | column |
| I6 | Balance | `iCurrentBalance` | column | column |
| I7 | Difficulty, save mode, guns, style, mods | `sInitialGameOptions`, `mods()` | tooltip (load only) | detail panel |
| I8 | Dead is Dead | `ubGameSaveMode` | skull icon | tag (+ Iron Man, Quick, Auto tags, new) |
| I9 | Saved at (new) | file modification time | — (only the sort) | column |
| I10 | Save mode hides quick/auto saves | `IsAutoSaveName/IsQuickSaveName` | not listed | not listed; count says so |
| I11 | Thumbnail (new) | — | — | see decisions |

## 3. Actions

| # | Action | Legacy input | Game function |
|---|---|---|---|
| A1 | Select | click, Up/Down | `gbSelectedSaveLocation` |
| A2 | Load | Load button, Enter, double-click | version/mods check → warning box → `StartFadeOutForSaveLoadScreen` → `LoadSavedGame` |
| A3 | Save over | Save button, Enter, double-click | confirm `SLG_CONFIRM_SAVE` → `DoSaveGame` |
| A4 | New save | select "Create new savegame", type name, Enter | `SaveNewSave` (file name from date + name) |
| A5 | Delete | Del | confirm `SLG_CONFIRM_DELETE` → delete file |
| A6 | Deselect / leave | Esc (first deselects), right click, Cancel | `LeaveSaveLoadScreen` |
| A7 | Scroll | wheel, arrows | |
| A8 | Load last at once | `gfLoadGameUponEntry` | skips the list |
| A9 | Sort, filter, show quick/auto (new) | — | view model only |

## 4. States
Load mode · Save mode · name entry · message boxes · fading out (loading) · Dead-is-Dead new game (save mode, name first).

## 5. Popups
Overwrite, delete, version/mods warning, load error, save error: native message box.

## 6. Cues
Fade out/in around loading; "Saving..." message.

## 7. Edge cases
- [ ] No saves; 200 saves (scroll); unreadable save (skipped, logged). [ ] Very long descriptions (ellipsis + tooltip).
- [ ] Iron Man in combat cannot save. [ ] Save from a different version or mods: warning.

## 8. Dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| Fixed 11 rows | scrolling table with sort (name, game time, sector, mercs, balance, saved) and a filter | modern list | owner |
| Options only in a tooltip | detail panel | readable | owner |
| Thumbnails | a 480 px wide PNG next to each save (`<save>.png`, `GetSaveThumbnailPath`) on the player's machine; the save format is unchanged. The picture is the map or tactical screen when the save is made there (quick saves), else the game as it was when the options or save screen was opened. Deleting a save deletes it. Saves without one show the sector's load screen art (`ubLoadScreenID` of the header) | owner |
| Loading | the native screen hands over to the legacy "load upon entry" path (`SaveLoadArmLoadUponEntry`), which loads with the legacy fades; the version/mods question is asked natively first | one load code path | — |
| Enter / Esc | act on key release, so that their release does not answer the next message box or reach the next screen | a legacy quirk avoided | — |

## 9. Parity tour

`tests/e2e/saveload_parity.lua` (goldens `save_native.png`, `load_native.png`, `mainmenu_lastsave.png`):

| Row | Covered by |
|---|---|
| A4, I10 | a new save named in the new-save row: the file exists (`ja2.saves()`), the list shows its name, sector and a thumbnail |
| A3 | save over it: the overwrite question, still two saves |
| I1, A9 | load mode: sort by name, the filter keeps one save |
| A5 | Esc drops the selection; Del asks, the file is gone |
| A2 | load: back on the map in A9 |
| A6 | Esc twice leaves to the options screen |
| layout | UI scale 150 %: layout audit |
| Continue | quit to the main menu; the last-save card; C loads the newest save |
| < 1280x720 | the legacy screen runs |

Not covered by a tour: the version/mods question (needs a save from another version) and the Dead is Dead new-game save.
