# Loading screen — functional spec (M1)

| | |
|---|---|
| Legacy code | `src/game/Loading_Screen.cc` (`GetLoadScreenID`, `DisplayLoadScreenWithID`), `src/game/Utils/Animated_ProgressBar.cc` (`CreateLoadingScreenProgressBar`, `SetRelativeStartAndEndPercentage`, `RenderProgressBar`) |
| Screen id | none: drawn straight into the frame buffer while a sector or a save loads (the game loop is blocked) |
| Shown when | entering a sector (`WorldDef.cc` load steps), loading a save (`SaveLoadGame.cc` steps) |
| Native code | `NativeUI::ShowLoadingScreen`, `LoadingProgress`, `LoadingStep` (`NativeUI.cc`, data model "loading"), `assets/ui/screens/loading.rml` |
| ui_mode key | `loadscreen` (default native) |
| Status | parity-complete (Phase 3); design and tips approved by the owner |

## 2. Information

| # | Information | Source | Legacy | Native |
|---|---|---|---|---|
| I1 | Art for the sector (day/night, town, SAM, desert, forest, mine, cave, heli at game start) | `GetLoadScreenID`, `loading-screens.json` + mapping | 640x480 image | full-bleed art from the player's data at runtime (`loadscreen-<id>`), shaded at the bottom |
| I2 | Progress | `SetRelativeStartAndEndPercentage` / `RenderProgressBar` | bar at 162,427 | thin bar across the bottom + % |
| I3 | Step text ("Loading struct layer...") | same calls' text (debug in legacy, only in some builds) | — / title | step label |
| I4 | Sector name, day, time, squad (new) | `GetSectorIDString`, game clock | — | title + chips |
| I5 | Tip (new) | new string table | — | "field note" card |

## 3. Actions
None (blocking). Tips change only between loads.

## 6. Cues
None; progress is drawn synchronously, so the native path must render a frame per `RenderProgressBar` call (software
path, like the legacy one).

## 7. Edge cases
- [ ] Missing art file: legacy prints "loadscreen data file not found" on black; native shows the gradient only.
- [ ] Modded extra load screens (index ≥ 28). [ ] Save load before any sector (heli art). [ ] Very fast loads.

## 8. Changed

Tips are **new content**: 40 short gameplay tips (`loading.tip.1..40` in `assets/ui/strings/strings-eng.json`),
English first; the other languages fall back to English and are marked for a translator (`_todo.phase3`). The day/time
chips and the sector title are new. Approved by the owner. The art covers the screen (aspect kept, cropped).

## 9. Parity tour

`tests/e2e/loadingscreen_parity.lua` (golden `loading_native.png`): `ja2.debug("loadscreen", id)` shows the native
screen with the bar at 60 %, a tip and the art; it is gone with the next frame; another id shows again; ui_mode legacy
draws the legacy screen. Real loads are exercised by every tour that starts a game or loads a save.
