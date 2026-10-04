# Intro / ending cinematic - functional spec (M1)

The `INTRO_SCREEN` presentation: the start-up splash, the new-game intro and the end-game ending cinematic. One
screen, three modes, each a fixed chain of Smacker videos with two skip keys. The native rebuild keeps the
videos (they are content) and rebuilds the *presentation* around them: a clean cinema stage with a fading hint
bar, a scene indicator and a still card when a video cannot be played.

| | |
|---|---|
| Legacy code | `src/game/Intro.cc` (entry `EnterIntroScreen()`, handler `IntroScreenHandle()` → `GetIntroScreenUserInput()` + `HandleIntroScreen()`, exit `ExitIntroScreen()`); videos `src/game/Utils/Cinematics.cc` |
| Screen id | `INTRO_SCREEN` (what `ja2.screen()` returns) |
| Reached from | start-up (`INTRO_SPLASH`, `src/sgp/SGP.cc`); new game (`FrontNewGame.cc` / legacy `GameInitOptionsScreen.cc` → `INTRO_BEGINING`); the Queen's death sequence (`src/game/Tactical/End_Game.cc` `DoneFadeOutEndCinematic()` → `INTRO_ENDING`) |
| Returns to | `INIT_SCREEN` (splash, beginning) · `EPILOGUE_SCREEN` (ending, see [epilogue.md](epilogue.md)) |
| Owner of this spec | adhikasp — scope and style chosen 2026-10-04 ("Cinema + victory epilogue", "Clean cinema + fading hints") |
| Status | reviewed |

## 1. How it was audited

- **Code read:** `Intro.cc` (the whole screen: `GetNextIntroVideo()` chains, `PrepareToExitIntroScreen()` exit
  targets, the input in `GetIntroScreenUserInput()` / `BackgroundRegionCallback()`), `Cinematics.cc`
  (`SmkPlayFlic()`: the video is blitted into `FRAME_BUFFER`, all smacker audio tracks are decoded and played,
  nearest-neighbour integer scaling on big screens), `End_Game.cc` (who enters the ending and when),
  `Game_Init.cc` (`ReStartingGame()`), `SGP.cc` (`-no-intro`).
- **Automation:** the legacy screen has no UI elements at all — `ja2ctl ui`/`text` show nothing: it is a video
  player with two invisible keys. There is nothing to dump. The chain is instead read as data from the view
  model (`ja2.viewModel("intro")`) in the parity tour.
- **Image usage:** `INTRODIR/*.smk` (`rebel_cr`, `omerta`, `prague_cr`, `prague`, `throne_mig`, `throne_nomig`,
  `heli_flyby`, `heli_sky`, `heli_nosky`, `splashscreen`) and `INTERFACEDIR/sirtechsplash.sti`. The videos are
  game data and are **not** recreated here (Phase 9, optional video).

## 2. Information shown

| # | Information | Source | Shown as (legacy) | Shown when | Notes |
|---|---|---|---|---|---|
| I1 | The current video | `SmkPlayFlic(gpzSmackerFileNames[i])` → `FRAME_BUFFER` | full-screen video, integer-scaled and centred | a scene is playing | content, unchanged |
| I2 | The Sir-Tech logo | `DisplaySirtechSplashScreen()` blits `sirtechsplash.sti` | static logo on black | end of `INTRO_SPLASH` | content, unchanged |
| I3 | Scene position *(native)* | the scene chain, `IntroModel` | "Scene 2 of 4" + four dots | during playback, with the hint bar | new |
| I4 | Progress of the scene *(native)* | frames played / frames in the flic | a 2 dp track along the bottom edge | during playback, with the hint bar | new; hidden when the frame count is unknown |
| I5 | Skip hint *(native)* | `Str("intro.hint")` | "Space / click — next scene · Esc — skip" | with the hint bar | new; the legacy keys were invisible |
| I6 | Still card *(native)* | a scene whose video cannot be opened | black card: "Scene 2 of 4", a short note | a video is missing or broken | new; see S6 |

## 3. Actions

Every input the legacy screen has. There are no buttons and no cursor (`VIDEO_NO_CURSOR`).

| # | Action | Input (legacy) | Game function | Preconditions / disabled when | Feedback |
|---|---|---|---|---|---|
| A1 | Skip the whole presentation | `Esc` (key up) | `PrepareToExitIntroScreen()` | always | at once: the exit screen |
| A2 | Skip the current video | `Space` (key up) | `SmkCloseFlic(gpSmackFlic)` | a video is playing | the next video at once |
| A3 | Skip the current video | any mouse button down, anywhere | `SmkCloseFlic(gpSmackFlic)` | a video is playing | the next video at once |

## 4. States and modes

| # | State | Entered by | Left by | Differences |
|---|---|---|---|---|
| S1 | splash (`INTRO_SPLASH`) | start-up (`SGP.cc`) | the one video ends, A1 | `splashscreen.smk`, then the Sir-Tech logo (I2) and `gfDoneWithSplashScreen = TRUE` → `INIT_SCREEN` |
| S2 | beginning (`INTRO_BEGINING`) | new game (`FrontNewGame.cc`, legacy `GameInitOptionsScreen.cc`) | the chain ends or A1 | `rebel_cr` → `omerta` → `prague_cr` → `prague` → `INIT_SCREEN` |
| S3 | ending (`INTRO_ENDING`) | `DoneFadeOutEndCinematic()` (`End_Game.cc`) | the chain ends or A1 | `throne_mig`/`throne_nomig` (Miguel dead?) → `heli_flyby` → `heli_sky`/`heli_nosky` (Skyrider dead?) → `EPILOGUE_SCREEN` |
| S4 | skipped (`-no-intro`, S1/S2 only) | `SetSkipIntroVideos(true)` | immediate | no video: exit at once (`EnterIntroScreen`). The ending still plays |
| S5 | skipped by the player | A1 | immediate | the rest of the chain does not play; the exit target is unchanged |
| S6 | video cannot be played | `SmkPlayFlic()` returns `nullptr` (missing, empty or broken flic — the Urban Chaos quirk in `Cinematics.cc`) | the card times out (4 s) or A2/A3 | the native screen shows the still card (I6) instead of black; the chain continues as if the video had played |

## 5. Popups, overlays and modals

None. The screen is the presentation; nothing can open over it.

## 6. Native design (M2)

Wireframes: `assets/ui/mocks/phase7/intro_stage.rml`, `intro_fallback.rml` (`tools/ui/phase7_mocks.py`), shown in
game with `ja2.debug("mock", "phase7/intro_stage")`.

**The stage (S1–S3).** The video keeps playing in the legacy layer, exactly where the legacy screen blits it
(the decoder and scaling are content and stay as they are). Over it, the native chrome:

```
┌────────────────────────────────────────────────────────────────────────────┐
│                                                                            │
│                                                                            │
│                     the video, integer-scaled, centred                     │
│                                                                            │
│                                                                            │
│                                                                            │
│  ● ● ○ ○   Scene 2 of 4        ·        Space / click — next scene · Esc — skip │
└────────────────────────────────────────────────────────────────────────────┘
```

- The hint bar docks at the bottom: scene dots + "Scene n of m" on the left, the skip hint on the right, over a
  fade-to-black scrim so it stays readable over any frame. A 2 dp progress track runs along the very bottom.
- **Fading hints.** The whole bar fades out (`--t-slow`) 2 s after the last mouse move or key press and fades
  back in on the next one. Untouched, the screen is pure picture. (Reduced motion: the fade is instant.)
- Nothing else is drawn: no cursor, no buttons, no branding. The videos carry their own audio and the screen
  plays no music (`MUSIC_NONE`, unchanged).

**The still card (S6).** A scene whose video cannot be opened draws a black card in the native layer:
the scene position in display type ("Scene 2 of 4") and one dim line ("video not available"), for 4 s or until
A2/A3, then the chain continues. This is what a data set without the smacker files looks like.

**Layout rules.** The bar is `--sp-4` inset from the bottom edge and grows with the window; on narrow layouts
(below 1200 dp) the hint text and the scene count shrink one step (`compact` class). The video is never scaled by
the UI layer: the legacy blitter decides its integer scale (2x/3x/…), the chrome only overlays it. (A toast in the
bottom-right corner can overlap the bar's right end; the bar is only up for the first two seconds after input, and
nothing raises a screen message during a presentation.)

## 7. Wiring, sounds and timing

- **Wiring.** `ui_mode` key `intro` (default native), route `{ INTRO_SCREEN, "intro", &CreateIntroScreen }`.
  Below 1280x720 or without RmlUi the legacy screen runs unchanged (parity). The native screen drives the same
  legacy entry points as the buttons did: `SmkPlayFlic`/`SmkCloseFlic`/`SmkPollFlics` for playback,
  `PrepareToExitIntroScreen()`'s exit targets for leaving.
- **The scene chain is a pure core** (`IntroModel`): mode + Miguel/Skyrider liveness → the ordered scene list
  and the exit screen. Unit-tested (`IntroModel_unittest.cc`); the screen only plays what it is given.
- **Sounds and timing.** No music (legacy: `SetMusicMode(MUSIC_NONE)`). The videos' own audio is unchanged
  (`SoundPlayFromSmackBuff`). Timing comes from the flic's frame rate (`SmkSkipFrames`); the hint bar's 2 s
  fade and the 4 s still card are the only new timers.
- **Deterministic surface.** `ja2.debug("intro", kind [, "still"])` opens the screen in any mode from anywhere
  (`kind` = `splash` | `beginning` | `ending`; `still` plays the chain as still cards instead of the videos — what a
  data set without the flics shows anyway, and how the tour gets through minutes of video deterministically);
  `ja2.viewModel("intro")` reports `kind`, the `scenes` rows, `index`, `position`, `progress`, `card`, `chrome`,
  `exit` and `finished`, and `ja2.viewModelCommand("intro", "next" | "skipall")` drives it.

## 8. Deliberately dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|
| Invisible skip keys | replaced by the fading hint bar (I5) | the keys stay the same, but a player must be able to see them | adhikasp, 2026-10-04 ("Clean cinema + fading hints") |
| `INTRO_ENDING` exits to `CREDIT_SCREEN` | exits to `EPILOGUE_SCREEN` first | the victory epilogue is the missing end-game screen ([epilogue.md](epilogue.md)); it restarts the game and hands over to the credits | adhikasp, 2026-10-04 ("Cinema + victory epilogue") |
| Black screen on a missing/broken video | the still card (I6) | a data set without the videos must still present something | this PR |
| Video scaling | unchanged (legacy integer scaling) | the video is content; Phase 9 may replace it | — |
| Below 1280x720 | the legacy screen runs, unchanged | native UI floor | — |

## 9. Parity tour

`tests/e2e/intro_parity.lua` (rows: the M1 checklist above).

| Row | Covered by (step / assertion) |
|---|---|
| S1, I2 | `ja2.debug("intro", "splash", "still")`: one scene (`splashscreen`), the exit is `INIT_SCREEN` |
| S2 | `… "beginning"`: the four scenes in order (`rebel-cr`, `omerta`, `prague-cr`, `prague`), the exit is `INIT_SCREEN` |
| S3 | `… "ending"`: three scenes with Miguel and Skyrider alive (`throne-mig`, `heli-flyby`, `heli-sky`), the exit is `EPILOGUE_SCREEN` |
| S3 (variants) | the same with both staged `dead = true`: `throne-nomig` and `heli-nosky` |
| I3, I5 | the hint bar is on screen, "Scene 1 of 3", the skip hints; after `Space` the position follows ("Scene 2 of 3") |
| I5 (fade) | the bar is up right after input, fades out after 2 s of idling, and comes back on a mouse move |
| I6, S6 | the still card is shown for a scene with no video, with no progress track (screenshot) |
| A2, A3 | `Space` and a click anywhere advance the scene (`m.index` grows) |
| A1 | `Esc` skips the rest of the chain and lands on `EPILOGUE_SCREEN`, the same target playing the chain would give |
| §S5 | `ui_mode intro=legacy` runs the legacy video player on the same screen id and hands over to the same epilogue |
| §S4 | below 1280x720 the native UI does not run: the legacy screen is used and skipped with `Esc` |
| — | resolution matrix: `ctest -L resolution` (1080p) and the full `ctest -R resolution_`; golden for the still card at 1280x720, 1920x1080, 2560x1080 and 3440x1440 |
