# Save / Load — functional spec (M1)

| | |
|---|---|
| Legacy code | `src/game/SaveLoadScreen.cc` (`SaveLoadScreenHandle`, `EnterSaveLoadScreen`, `GetSaveLoadScreenUserInput`, `SaveLoadSelectedSave`, `DisplaySaveGameEntry`) |
| Screen id | `SAVE_LOAD_SCREEN` (mode `gfSaveGame`) |
| Reached from | main menu Load / C / Alt+C; options Save/Load; new game (Dead is Dead asks for a name) |
| Returns to | `guiPreviousOptionScreen`; after loading, the saved screen |
| ui_mode key | `saveload` (planned) |
| Status | draft: wireframes `assets/ui/mocks/phase3/saveload.rml`, `saveload_save.rml` |

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

## 8. Dropped or changed (proposed)

| Item | Decision | Reason |
|---|---|---|
| Fixed 11 rows | scrolling table with sort and filter | modern list |
| Options only in a tooltip | detail panel | readable |
| Thumbnails | proposed: a small screenshot written next to the save (`<save>.png`, player's machine only, save format unchanged); mock shows the sector's load screen art as a stand-in | owner decision |

## 9. Parity tour
`saveload_parity.lua` (M4): save new, overwrite, load (state matches), delete (file gone), Esc behaviour, version warning.
