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
| I7 | The rebindable actions and the key each runs | `KeyBindings` (`src/game/NativeUI/KeyBindings.cc`), whose sources are the tactical command table (`NativeUI/TacticalCommands.cc`) and the strategic map's action table (`NativeUI/MapCommands.cc`) | keys typed into the screen's own switch (no table) | Controls page: a row per action, grouped, with the key it runs and a reset |

Toggle placement: **Gameplay** Show Misses, Hide Bullets, Tracking Mode, Real Time Confirmation, Show Movement Path,
sleep/wake notifications, Metric, Blood n Gore. **Video** Animate Smoke, Tree Tops, Wireframes, Items Glow, 3D Cursor, Lights
under Mercs, Merc Lights during Movement. **Audio** Speech, SubTitles, Pause Text Dialogue, Mute Confirmations.
**Controls** Never Move My Mouse, Old Selection Method, Snap Cursor to Mercs/Doors, a rebindable list of every action a
table owns, and a read-only reference for the shortcuts no table drives yet.
**Accessibility** native UI scale, reduced motion (new), SubTitles and Pause Text Dialogue mirrored.

### 2.1 Where a binding lives

The rebindable actions are one registry (`KeyBindings::Bindings()`): a screen hands its actions in as a *source* with
its default key, and the registry is the only place that knows every action and resolves a conflict. The tactical HUD's
command table is the first source; the strategic map's action table (`NativeUI/MapCommands.cc`) is the second, and the
rules below are the ones that govern both.

A key runs one action **per screen**. The two screens' legacy keys collide on plenty of letters - `c` contracts on the
map and crouches in tactical, space is the map's clock and the tactical HUD's next merc, F1 to F6 sort the map's team
list and select the HUD's first six mercs - and a single global key space would have to re-key one of the two screens to
say anything. So an action belongs to the screen that registered it, and a name **two** sources declare is one action on
both (`shared`): it keeps one key and a bind moves it on every screen that runs it (`squad.select.4`, `nav.options`).
The conflict check is still the registry's, over the actions of the screen the key belongs to, and never a second check
inside a screen's own table.

A binding is an **override of the source's default**, saved in `ja2.json` under `key_bindings` (action name -> key, `""`
for bound to nothing) beside `native_ui_scale` and `reduced_motion`. Only what differs from the default is written, so a
player who never rebinds keeps the file they had. Reading happens once at startup (`SGP.cc`), writing through
`ContentManager::saveKeyBindings`; a headless run never writes it (`FrontOptions.cc` guards every save with
`!sgp::IsHeadless()`), which is why the tour proves the binding is *applied*, not that the file was written - the file
round trip is a Rust unit test.

**Conflicts: one owner, and a key never runs two actions of one screen.** Binding a key another action of that screen
holds moves the key to the new action and leaves the other one unbound; the page marks that row and names it in a
warning. Reset gives an action its default key back, taking it from whoever holds it now - and when it takes one, it is
reported the same way, so a reset can never silently cost a neighbour its key. A saved `key_bindings` entry that another
action of the same screen also claims takes it from that action too, so the file cannot produce two actions on one key
either. A key that is not a key (an unknown name, a modifier on its own, a key that has no name) is refused.

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
| A11 | Rebind an action | click its key on the Controls page, then press a key; Esc cancels | `KeyBindings::Bind`, and the next key press runs the action |
| A12 | A key another action holds | — | the key moves to the new action; the action that had it is left unbound and named in a warning |
| A13 | Reset a binding | the row's reset, or Reset all | `KeyBindings::Reset` / `ResetAll`: the defaults come back, and a row reset that takes a key from another action names it |
| A14 | A key that cannot be bound | — | refused with a note, the capture stays open |

On exit the legacy screen re-applies tree tops, item glow and smoke when a world is loaded (`ExitOptionsScreen`); the
native screen calls the same code.

## 4. States
S1 from main menu (Save disabled, Quit hidden, no campaign panel) · S2 in game · S3 message box (quit / speech rule) · S4 a
video change pending (applied at the start of the next frame; the page reads the settings back then) · S5 a key is being
captured (the page takes every key; Esc cancels) · S6 a row lost its key to another action (marked, named in the warning).

## 5. Popups
Quit confirmation (yes/no), speech/subtitle warning (ok): native message box.
Key capture ("Press a key"): a modal over the Controls page, with the action's name and Cancel.

## 6. Cues
Toggle click sound, slider sample sounds, music volume live.

## 7. Edge cases
- [ ] Iron Man in combat: Save disabled. [ ] Headless: video changes only change the canvas.
- [ ] Long toggle labels (German) wrap in the row. [ ] 640x480: legacy screens.
- [x] A key another action already holds is not refused: it moves to the new action and the other one is left unbound and
  named in the warning. A key that cannot be bound (a modifier on its own, an unnamed key) is refused and the capture
  stays open. A binding saved last session is applied before the first frame (`ja2.json` `key_bindings`).

## 8. Dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| Separate video sub-screen with Apply | folded into the Video page, applied at once (persisted as before) | one place for settings | owner |
| Help only as tooltip | a side panel for the hovered or focused setting | readable | owner |
| Key reference, reduced motion, interface size, campaign panel | added; reduced motion and the interface size are saved to ja2.json (`reduced_motion`, `native_ui_scale`) | accessibility / info | owner |
| Key rebinding | shipped for the actions the tables own: the tactical command table (#48 slice 1) and the strategic map's action table (slice 2); the remaining per-screen shortcuts still read raw key codes and stay a read-only reference until their slices land | the other screens have no action table yet | owner (scope C, sliced) |
| Toggle sounds and slider samples | kept (the samples play when a slider stops, and on the play buttons) | parity | — |
| Esc | acts on key release (legacy: press), so that its release does not reach the main menu, which quits on release | a legacy quirk the native path avoids | — |

## 9. Parity tour

`tests/e2e/options_parity.lua` (goldens `options_gameplay/audio/video/controls/access/ingame.png` and, for the
rebinding states, `options_bind_capture/done/conflict.png`). Game state is read
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
| I7, A11 | the Controls page lists every action of `ja2.bindings()`; clicking a row's key opens the capture modal, the next key lands on the row, and `ja2.tactical.key` runs it |
| A12 | binding a key another action holds: `ja2.bind` moves it, names the loser, and the page marks that row and warns |
| A13, A14 | the row's reset takes its default key back and names whoever lost it, Reset all restores every default, and a key that cannot be bound is refused |
| I7 (map) | the map's rows are on the same page (`options_controls_map.png`), the reference list is down to the tactical screen's four raw shortcuts, the capture modal opens on a map action (`options_bind_map_capture.png`) and the rebind lands on its row (`options_bind_map_done.png`) and on the map screen's own dispatch |
| A7 | Done returns to the main menu; the volume stays |
| S2, I6 | in game: saving available, the campaign panel shows the difficulty |
| A5 | Quit: No stays, Yes goes to the main menu |
| < 1280x720 | the legacy screen runs |

`tests/e2e/tactical_commands.lua` takes the last step: it rebinds crouch to `j`, presses `j` in the tactical screen and
checks the merc crouched — the binding, applied through the same registry `ForKey` reads. `tests/e2e/map_keys.lua` does
the same for the map's table: every group of shortcuts by key, the effect asserted on the game, a rebind on one leaving
the other screen's key alone, and a shared action (a squad) moving on both screens at once.

