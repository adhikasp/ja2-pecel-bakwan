# Setup — functional spec (M1)

> Parity contract for the native pre-game setup screen, which replaces the FLTK launcher
> (`docs/plan/native-modern-game.md`, "Native front end"). It is shown before any game data is loaded,
> so it cannot use the content manager, the game loop or the legacy screens.

| | |
|---|---|
| Legacy code | `src/launcher/Launcher.cc` (removed), `src/launcher/StracciatellaLauncher.fl` |
| Reached from | automatically at startup when `game_dir` is unset or has no `Data/` directory; forced with `JA2_SETUP=1` |
| Returns to | the operating system, or the game itself (the process is relaunched with the new configuration) |
| Native code | `src/game/NativeUI/FrontSetup.cc` (`SetupViewModel`, "setup"), `assets/ui/screens/setup.rml`; the loop lives in `NativeUI::RunSetup` (`src/game/NativeUI/NativeUI.cc`) |
| ui_mode key | `setup` (default native; `setup=legacy` keeps the old "failed to load game data" exit) |
| Status | implemented with the issue; the game persists through `EngineOptions`/`ja2.json` |

## 1. Audit

The launcher wrote `ja2.json` and then spawned `ja2` as a child process (`Launcher::startExecutable`).
Its surface, moved here: game directory + validation, save-game directory, resource version + guessing,
the mod manager (available/enabled, enable/disable, reorder, details), the logs viewer, and launching the
game. The video/audio/gameplay/controls pages were already native (`docs/ui/options.md`); this screen does
not repeat them.

## 2. Information

| # | Item | Source | Launcher | Native |
|---|---|---|---|---|
| I1 | Game directory | `EngineOptions_getVanillaGameDir` | text input + native folder chooser | text input + `SDL_ShowOpenFolderDialog` |
| I2 | Save-game directory | `EngineOptions_getSaveGameDir` | text input + chooser | text input + folder dialog |
| I3 | Resource version | `EngineOptions_getResourceVersion`, `VanillaVersion_toString` | dropdown | dropdown, same predefined order |
| I4 | Version guess | `guessResourceVersion` | button | button |
| I5 | Available/enabled mods | `ModManager_*`, `EngineOptions_*Mod*` | two browsers | two lists |
| I6 | Mod details | `Mod_getName` / `Mod_getVersionString` / `Mod_getDescription` | details box | details panel |
| I7 | Last log | `Logger_getFilePath("ja2.log")` | Logs tab | Logs tab |
| I8 | Validation | `checkIfRelativePathExists(dir, "Data")`, `Data/Ja2Set.dat.xml` (1.13) | warning dialogs | status line + 1.13 warning |

## 3. Actions

| # | Action | Game function |
|---|---|---|
| A1 | Choose a directory | `SDL_ShowOpenFolderDialog`; the result is applied on the next frame |
| A2 | Edit a directory by hand | bound to the field; validates on change |
| A3 | Pick a version | `EngineOptions_setResourceVersion` on Apply; Simplified Chinese force-enables its localisation mod, any other version disables it (as the launcher) |
| A4 | Guess version | `guessResourceVersion` |
| A5 | Enable / disable a mod | `EngineOptions_pushMod` / `clearMods` on Apply |
| A6 | Reorder an enabled mod | moves it in the list; order is written on Apply |
| A7 | Apply | `EngineOptions_write` (`ja2.json`) |
| A8 | Start game | relaunch the process (`execv`) so the content manager reloads |
| A9 | Quit | exit |
| A10 | Refresh logs | read `ja2.log` again |

## 4. States

S1 game tab, no directory (missing status) · S2 valid directory (ok status) · S3 1.13 detected (warning) ·
S4 version guessed · S5 mods, one selected with details · S6 logs · S7 saved ("Saved to ja2.json", Start enabled).

## 5. Startup path

`SGP.cc` checks `game_dir` after `EngineOptions_create` (which no longer fails on a missing game dir).
When it is unusable - or `JA2_SETUP` is set - the video manager is brought up without a content manager and
`NativeUI::RunSetup` drives the screen until the player saves and restarts or quits. On "Start game" the
process is replaced, so the normal startup runs again with the new `ja2.json`. Headless and driven sessions
keep the old path (they cannot show the screen).

## 6. Screenshot mode

For goldens and CI: `JA2_SETUP=1 JA2_SETUP_SHOT=out.png [JA2_SETUP_TAB=game|mods|logs] ja2 -res WxH`
opens the screen, captures after a few frames and quits.
