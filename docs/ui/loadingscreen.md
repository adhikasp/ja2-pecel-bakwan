# Loading screen — functional spec (M1)

| | |
|---|---|
| Legacy code | `src/game/Loading_Screen.cc` (`GetLoadScreenID`, `DisplayLoadScreenWithID`), `src/game/Utils/Animated_ProgressBar.cc` (`CreateLoadingScreenProgressBar`, `SetRelativeStartAndEndPercentage`, `RenderProgressBar`) |
| Screen id | none: drawn straight into the frame buffer while a sector or a save loads (the game loop is blocked) |
| Shown when | entering a sector (`WorldDef.cc` load steps), loading a save (`SaveLoadGame.cc` steps) |
| ui_mode key | `loadscreen` (planned) |
| Status | draft: wireframe `assets/ui/mocks/phase3/loading.rml` |

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

## 8. Changed (proposed)
Tips are **new content** (≈40 short gameplay tips, English first, translation later). Sector/time chips new.

## 9. Parity tour
`loading_parity.lua`: `ja2.debug("loadscreen", id)` shows the right art id; progress reaches 100 on a sector load.
