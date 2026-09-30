# Options — functional spec (M1)

> Parity contract for the native options screen, which also absorbs the video options screen.

| | |
|---|---|
| Legacy code | `src/game/Options_Screen.cc` (`OptionsScreenHandle`, `EnterOptionsScreen`, `ExitOptionsScreen`, `HandleOptionToggle`), `src/game/VideoOptionsScreen.cc` |
| Screen ids | `OPTIONS_SCREEN`, `VIDEO_OPTIONS_SCREEN` |
| Reached from | main menu Preferences / O; map screen and tactical Options button / O |
| Returns to | `guiPreviousOptionScreen`; `SAVE_LOAD_SCREEN` (save/load), `MAINMENU_SCREEN` (quit) |
| ui_mode key | `options` (planned; covers both screens) |
| Status | draft: wireframes `assets/ui/mocks/phase3/options*.rml` |

## 1. Audit
`tests/e2e/phase3_audit.lua` states `options`, `options_hover`, `options_ingame`, `video_options`. Code read: both files;
`GameSettings.h` (`TOPTION_*`), `SaveGameSettings`, `VideoSetDisplaySettings`. Images: `optionscreenbase.sti`,
`MLG_OPTIONHEADER`, `optionscheckboxes.sti`, steel slider art: all replaced by components.

## 2. Information and settings

| # | Item | Source | Legacy | Native page |
|---|---|---|---|---|
| I1 | 23 toggles `TOPTION_SPEECH` … `TOPTION_TRACKING_MODE` | `gGameSettings.fOptions[]`, labels `zOptionsToggleText`, help `zOptionsScreenHelpText` | two columns of check boxes, help as tooltip | split by topic (below); help in a side panel on hover/focus and as tooltip |
| I2 | Effects / Speech / Music volume (0–127) | `GetSoundEffectsVolume`, `GetSpeechVolume`, `MusicGetVolume` | vertical steel sliders | Audio: horizontal sliders in % with a sample button |
| I3 | Resolution, window mode, UI scale, world zoom, filter, FPS cap | `VideoGetDisplaySettings`, `VideoGetScaleQuality`, `VideoGetTargetFPS` | video sub-screen, click to cycle + Apply | Video page: dropdowns/segments, applied at once |
| I4 | "In effect now" (window, canvas, UI scale, world layer) | video globals | left text block | note on the Video page |
| I5 | Save Game available | in a game and not Iron Man in combat (`CanGameBeSaved`) | button disabled | button disabled + tooltip |
| I6 | Campaign settings (new, read-only) | `gGameOptions` | — | Gameplay page panel |

Toggle placement: **Gameplay** Show Misses, Hide Bullets, Tracking Mode, Real Time Confirmation, Show Movement Path,
sleep/wake notifications, Metric, Blood n Gore. **Video** Animate Smoke, Tree Tops, Wireframes, Items Glow, 3D Cursor, Lights
under Mercs, Merc Lights during Movement. **Audio** Speech, SubTitles, Pause Text Dialogue, Mute Confirmations.
**Controls** Never Move My Mouse, Old Selection Method, Snap Cursor to Mercs/Doors, plus a read-only key reference.
**Accessibility** native UI scale, reduced motion (new), SubTitles and Pause Text Dialogue mirrored.

## 3. Actions

| # | Action | Legacy input | Game function |
|---|---|---|---|
| A1 | Toggle an option | click box or label | `HandleOptionToggle` (plays a click; Speech+SubTitles rule, A6) |
| A2 | Volume | drag slider | `SetSoundEffectsVolume` / `SetSpeechVolume` / `MusicSetVolume`; a sample plays after a pause (`HandleSliderBarMovementSounds`) |
| A3 | Save Game | button, S | `gfSaveGame = TRUE`, `SAVE_LOAD_SCREEN` |
| A4 | Load Game | button, L | `gfSaveGame = FALSE`, `SAVE_LOAD_SCREEN` |
| A5 | Quit to main menu | button → yes/no box `OPT_RETURN_TO_MAIN` | `ConfirmQuitToMainMenuMessageBoxCallBack` |
| A6 | Speech and SubTitles both off | — | refused with message `OPT_NEED_AT_LEAST_SPEECH_OR_SUBTITLE_OPTION_ON` |
| A7 | Done / Esc | button, Esc | back to `guiPreviousOptionScreen`; `SaveGameSettings()` on exit |
| A8 | Video options | Video button, V | Video page (key 2; V kept) |
| A9 | Change a video setting | click to cycle, then Apply | `VideoSetDisplaySettings` + persist to ja2.json; revert timer if the mode fails |
| A10 | Pages (new) | — | 1–5, Tab / arrows / Space |

On exit the legacy screen re-applies tree tops, item glow and smoke when a world is loaded (`ExitOptionsScreen`); the
native screen calls the same code.

## 4. States
S1 from main menu (Save disabled, no campaign panel) · S2 in game · S3 message box (quit / speech rule) · S4 video mode
change pending revert.

## 5. Popups
Quit confirmation (yes/no), speech/subtitle warning (ok): native message box.

## 6. Cues
Toggle click sound, slider sample sounds, music volume live.

## 7. Edge cases
- [ ] Iron Man in combat: Save disabled. [ ] Headless: video changes only change the canvas.
- [ ] Long toggle labels (German) wrap in the row. [ ] 640x480: legacy screens.

## 8. Dropped or changed (proposed)

| Item | Decision | Reason |
|---|---|---|
| Separate video sub-screen with Apply | folded into the Video page, applied at once (same persist + revert) | one place for settings |
| Help only as tooltip | also a side panel | readable |
| Key reference, reduced motion, campaign panel | added | accessibility / info |
| Key rebinding | not in Phase 3 | the game has no remap layer yet |

## 9. Parity tour
`tests/e2e/options_parity.lua` (M4): every toggle by id flips `gGameSettings.fOptions` (via `ja2.eval`), the speech rule,
volumes, video change via the page, save/load/quit routing, settings persisted after restart.
