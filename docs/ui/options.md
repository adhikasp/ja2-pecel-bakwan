# Options — functional spec (M1)

> Parity contract for the native options screen, which also absorbs the video options screen.

| | |
|---|---|
| Legacy code | `src/game/Options_Screen.cc` (`OptionsScreenHandle`, `EnterOptionsScreen`, `ExitOptionsScreen`, `HandleOptionToggle`), `src/game/VideoOptionsScreen.cc` |
| Screen ids | `OPTIONS_SCREEN`, `VIDEO_OPTIONS_SCREEN` |
| Reached from | main menu Preferences / O; map screen and tactical Options button / O |
| Returns to | `guiPreviousOptionScreen`; `SAVE_LOAD_SCREEN` (save/load), `MAINMENU_SCREEN` (quit) |
| Native code | `src/game/NativeUI/FrontOptions.cc` (`OptionsViewModel`, "options"), `assets/ui/screens/options.rml` |
| ui_mode key | `options` (default native; it covers both screens: the legacy Video screen is only reached from the legacy options screen) |
| Status | parity-complete (Phase 3); design approved by the owner |

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
| A9 | Change a video setting | click to cycle, then Apply | `RequestVideoSettings` (applied at the next frame, persisted to ja2.json); FPS with `VideoSetTargetFPS` (not persisted, as legacy) |
| A10 | Pages (new) | — | 1–5, Tab / arrows / Space |

On exit the legacy screen re-applies tree tops, item glow and smoke when a world is loaded (`ExitOptionsScreen`); the
native screen calls the same code.

## 4. States
S1 from main menu (Save disabled, Quit hidden, no campaign panel) · S2 in game · S3 message box (quit / speech rule) · S4 a
video change pending (applied at the start of the next frame; the page reads the settings back then).

## 5. Popups
Quit confirmation (yes/no), speech/subtitle warning (ok): native message box.

## 6. Cues
Toggle click sound, slider sample sounds, music volume live.

## 7. Edge cases
- [ ] Iron Man in combat: Save disabled. [ ] Headless: video changes only change the canvas.
- [ ] Long toggle labels (German) wrap in the row. [ ] 640x480: legacy screens.

## 8. Dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| Separate video sub-screen with Apply | folded into the Video page, applied at once (persisted as before) | one place for settings | owner |
| Help only as tooltip | a side panel for the hovered or focused setting | readable | owner |
| Key reference, reduced motion, interface size, campaign panel | added; reduced motion and the interface size are saved to ja2.json (`reduced_motion`, `native_ui_scale`) | accessibility / info | owner |
| Key rebinding | not in Phase 3 | the game has no remap layer yet | owner |
| Toggle sounds and slider samples | kept (the samples play when a slider stops, and on the play buttons) | parity | — |
| Esc | acts on key release (legacy: press), so that its release does not reach the main menu, which quits on release | a legacy quirk the native path avoids | — |

## 9. Parity tour

`tests/e2e/options_parity.lua` (goldens `options_gameplay/audio/video/controls/access/ingame.png`). Game state is read
through a view model made for the check (`ja2.viewModel("options")` outside the screen reads `gGameSettings`, the
volumes, the video settings and `gGameOptions`):

| Row | Covered by |
|---|---|
| S1, I5 | from the main menu: `can_save` false, the save command does nothing |
| I1, A1 | each of the 21 toggles besides Speech/SubTitles flips its setting and back, on its page; the help panel names it |
| A6 | Speech off, then SubTitles off is refused with the legacy message; SubTitles stays on |
| I2, A2 | volumes by command and by a click on the middle of a slider |
| I3, A9 | the resolution list opens; interface size 150 % applies at once (`ja2.nativeUi().uiScale`), layout audit, back to 100 % |
| A10 | keys 2, 4, 5 change pages; reduced motion on/off |
| A7 | Done returns to the main menu; the volume stays |
| S2, I6 | in game: saving available, the campaign panel shows the difficulty |
| A5 | Quit: No stays, Yes goes to the main menu |
| < 1280x720 | the legacy screen runs |
