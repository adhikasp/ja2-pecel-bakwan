# Driving the game headless: scripts, sessions and `ja2ctl`

The game can run with no window and no audio device, driven by a Lua script
or, command by command, from the shell. It is the real game (same screens, same
rules, same save files), not a mock, so this is useful for end-to-end tests,
reproducing bugs, and letting an agent play or inspect the game.

```bash
python tools/ja2ctl.py start                   # boot a headless session (to the main menu)
python tools/ja2ctl.py ui                      # what can be clicked right now?
python tools/ja2ctl.py text                    # what text is on screen?
python tools/ja2ctl.py click "New Game"        # click by label (or: click 320 240)
python tools/ja2ctl.py shot                    # screenshot -> prints the PNG path
python tools/ja2ctl.py state                   # screen, time, money, sector, mercs
python tools/ja2ctl.py stop

python tools/ja2ctl.py run tests/e2e/new_game_to_tactical.lua --isolated   # one-shot script
```

The design rationale is in [plan/headless-automation.md](plan/headless-automation.md).

## How it works

- **Virtual time.** In automation mode the clock only moves when the driver
  steps frames (1/60 s of game time per frame by default). Between commands the
  game is frozen, and when a script waits it runs as fast as the CPU allows:
  starting a campaign, hiring a merc and landing in Omerta takes about 45 s
  of game time and under 2 s of wall-clock time.
- **Headless.** No window or renderer is created. The game still draws every
  frame into its CPU-side frame buffer, so screenshots and pixel reads are
  exact. Composing the finished picture (the dirty regions, then the native UI
  layer blended over them) only happens when something reads it — a
  screenshot, a pixel, the text on screen — so stepping a wait of thousands of
  frames costs no pixel work at all, which is what keeps the resolution matrix
  fast. Animations follow the same rule: a transition that blocks the game loop
  (the sector-load zoom, the pre-battle and laptop ones) draws nothing for a
  headless run — its frames are rendered for nobody, since a read cannot happen
  while it runs — and only its end state is drawn; a loading screen draws one
  frame per progress step to a window and none to a headless run. Each frame is
  still *counted*, so the animation costs exactly the game time it always did.
  Pass `-show` (or `ja2ctl ... --show`) to watch in a window instead; a
  window presents every frame as it is stepped.
- **Real sound timing without a device.** Sounds are consumed in step with
  virtual time, so speech, dialogue and "is the merc still talking" behave as
  in normal play.
- **Reproducible.** The random number generator is seeded (`-seed`, default
  1), and real keyboard/mouse input is ignored while a driver is in control.
  The same script with the same seed produces the same frames
  (`tests/e2e/check_determinism.py` checks this).
- **Reading the screen as data.** Every string the game prints is recorded
  and checked against the final frame, so `texts()` returns what is actually
  visible. Every clickable mouse region and button is listed with a label:
  its text, the caption drawn on or beside it (like an HTML `<label>`), its
  tooltip, or a name set in code (`button->SetName("Close help")`) for
  image-only widgets.
- **Knowing when the game is idle.** "Idle" means no screen change or fade
  pending, no laptop page "loading", no window animating, nobody talking,
  no helicopter drop, nobody walking, and no enemy turn in progress, for a
  few frames in a row. `waitIdle()` replaces fixed sleeps.

## Command line

Flags for the `ja2` binary (any other flag, such as `-res 1280x720` or
`-gamedir`, works as usual):

| Flag | Meaning |
|---|---|
| `-run FILE` | Run a Lua script (or a legacy `.txt` e2e script) and exit with its result. `-uitest FILE` is an alias. |
| `-serve PORT` | Serve requests on `127.0.0.1:PORT` (`0` = any free port) until told to shut down. |
| `-session-file F` | With `-serve`: write `{"port":..,"pid":..}` to F once listening. |
| `-load SAVE` | Load this save (file name without `.sav`) before handing over. |
| `-seed N` | Random seed (default 1). |
| `-show` | Show a window instead of running headless. |
| `-home DIR` | Use DIR instead of the normal config/save directory (same as `JA2_HOME=DIR`). |
| `-log FILE` | Write the log to FILE. Without it the game logs to `ja2.log` in the temp directory, shared by everything that runs; `ja2ctl` always passes its own. |
| `-out DIR` | Where relative screenshot paths go. |
| `-arg VALUE` | Passed to the script as `ja2.args[n]`. Repeatable. |
| `-lua-path DIR` | Also look for `require()`d modules in DIR. Repeatable. |
| `-no-intro` | Skip the splash screen and intro videos. |
| `-frame-ms MS` | Game time per frame (default 16.667). |
| `-timeout S` | Kill the process after S seconds of wall-clock time. |
| `-freeze-wall-clock INSTANT` | Pin the **wall** clock (as opposed to `-frame-ms`, which pins game time): seconds since the epoch, or an ISO-8601 UTC instant like `2001-02-03T04:05:06Z`. For the time the UI *shows* — a save's filename, its "Today 19:47" — rather than time the engine measures. A frozen instant is always rendered in UTC, so the machine's timezone cannot change what is printed. |
| `-version-label TEXT` | The version string the UI shows. The default is the build's, `git rev-parse --short HEAD` included, which is right for a bug report but makes any screen showing it differ per commit. |

The last two exist so a run is reproducible on another machine; see
[tests/e2e/golden/README.md](../tests/e2e/golden/README.md#why-a-golden-image-is-portable),
which is what `check_resolution.py` passes them for.

The driver takes over once the game is ready: at the main menu, or in the
loaded save with `-load`.

Exit codes: `0` passed, `1` an expectation failed, `2` script or setup error,
`3` the game threw, `4` a wait timed out (or the watchdog fired). A failing
script prints the error with a Lua stack trace and saves `failure.png` in the
output directory.

## `ja2ctl`

`tools/ja2ctl.py` (Python 3, standard library only) manages sessions and talks
to them. It finds the binary in `_bin/` or `build/` (or `$JA2_BIN`) and on
Windows adds the MSYS2 MinGW runtime to `PATH` for the game.

Each worktree gets sessions of its own: the session root and the default session
name are derived from the checkout (`~/.ja2ctl/sessions/<worktree-id>/<name>`,
where `<worktree-id>` is the repo folder name plus a short hash of its path), so
two agents in two worktrees never see, stop or reuse each other's sessions.
Override the root with `$JA2CTL_SESSIONS` and the name with `-s NAME` or
`$JA2CTL_SESSION`.

A session is a directory of its own: a home directory (a `ja2.json` pointing at
the game data from your normal `ja2.json`), save games, screenshots, and its own
`ja2.log` — `ja2ctl log` prints it, so nothing runs against a shared log file.
Sessions are independent processes, each on a free port handed out by the OS
(never a fixed one), so any number can run side by side. A session whose game
process has died is reaped on the next `start` or `stop --all`.

| Command | |
|---|---|
| `start [--load SAVE\|FILE.sav] [--seed N] [--show] [--res WxH] [--saves DIR]` | Start a session. `--load` also accepts a path to a `.sav` file. `--saves` shares a save directory. |
| `stop [--all]`, `list` | `--all` stops this worktree's sessions and reaps the dead ones. |
| `log [-n N] [--path]` | The session's game log, last N lines, or just its path. Works after the session has stopped. |
| `screen`, `state`, `ui [--all]`, `text` | Look. Native elements show as `native` with their `#id`. |
| `audit` | Layout problems: legacy regions off screen and the native layout audit. |
| `native`, `uimode KEY legacy\|native\|default`, `uiscale S`, `vm NAME` | The native UI: state, per-screen `ui_mode`, UI scale, a view model's fields. |
| `click / rclick / dblclick / hover / wait-for / find TARGET [--exact] [--index N] [--id]` | TARGET is a label or text, `X Y`, or a native element id as `#credits.back` (or with `--id`). |
| `wait-gone TARGET` | |
| `key COMBO [TIMES]`, `type TEXT` | Keys such as `ESC`, `enter`, `space`, `pause`, `alt+c`, `ctrl+s`. |
| `move X Y`, `drag X0 Y0 X1 Y1`, `wheel DY` | |
| `step [N]`, `wait MS`, `wait-idle [MS]`, `wait-screen NAME [MS]` | Advance game time. |
| `shot [PATH]`, `pixel X Y` | |
| `saves`, `save NAME [DESC]`, `load NAME` | |
| `eval LUA` | Run any Lua in the session; `return` a value to print it. |
| `run SCRIPT [--isolated] [--load ..] [--out DIR] [--arg V]` | One-shot run without a session. `--isolated` uses a fresh throwaway home. Every run writes a log of its own (never the shared `ja2.log` of the temp directory); a failing run keeps it and prints the path. |

Every reply ends with a status line such as
`[MAP_SCREEN frame 545 t=9099ms idle]`. `--json` prints the raw reply.

`ja2ctl start` passes `-lua-path tests/e2e`, so the test helpers work
interactively:

```bash
python tools/ja2ctl.py eval 'return require("lib.campaign").startWithMerc("Barry").sector'
```

## The Lua API

Scripts get a global `ja2` table. Waits and input advance game time; anything
that can wait takes an optional timeout in milliseconds of game time.

**Time**

| | |
|---|---|
| `ja2.step([n])` | Advance n frames (default 1); returns the frame number. |
| `ja2.wait(ms)` | Advance ms of game time. |
| `ja2.waitIdle([timeout])` | Until the game is idle (see above). |
| `ja2.waitScreen(name, [timeout])` | Until `ja2.screen() == name` and idle. |
| `ja2.waitFor(locator, [timeout])` | Until the locator resolves to something enabled; returns it. |
| `ja2.waitGone(locator, [timeout])` | Until it no longer resolves. |
| `ja2.waitUntil(fn, [timeout], [what])` | Until `fn()` returns true. |
| `ja2.waitPixel(x, y, "#rrggbb", [tol], [timeout])`, `ja2.waitStable(x, y, [timeout])` | Pixel-based waits. |
| `ja2.frame()`, `ja2.time()` | Frames stepped, game milliseconds elapsed. |
| `ja2.reseed([seed])` | Reset the RNG to a defined state: everything after a flow point is a function of `seed` (or the run's `-seed`) alone, not of how many incidental numbers the screens driven before it drew. The campaign steps call it at a new game and entering the map; staging a battle reseeds itself ([`e2e-tactical-battles.md`](plan/e2e-tactical-battles.md), issue #177). |
| `ja2.composes()` | Full frames rendered (composes) since start-up: a headless session composes on demand, so this is what a load or an animation is allowed to cost. |

**Input.** Each call takes the frames a real user would take (move, press,
release).

| | |
|---|---|
| `ja2.click(locator \| x, y, [{button="right", count=2, timeout=ms}])` | Waits for the target, then clicks its centre. Returns what was clicked. |
| `ja2.rclick`, `ja2.dblclick`, `ja2.hover` | Same arguments. |
| `ja2.move(x, y)`, `ja2.mousedown([btn])`, `ja2.mouseup([btn])`, `ja2.drag(x0, y0, x1, y1)`, `ja2.wheel(dy, [x, y])`, `ja2.mouse()` | |
| `ja2.key(combo, [times])`, `ja2.keydown(key)`, `ja2.keyup(key)` | `"ESC"`, `"enter"`, `"f1"`, `"a"`, `"alt+c"`, `"shift+tab"`, ... |
| `ja2.type(text)` | Types like a keyboard: a key event and a text event per character. |

**Locators.** Either a string (case-insensitive substring of a label, button
text, tooltip, name, or any text on screen), or a table:
`{text=..., exact=true, index=2, within={x=,y=,w=,h=}}`, `{id="credits.back"}` (a native element by id) or `{x=, y=}`. Whole
matches win over partial ones, then reading order. Only things a click would
actually reach count: a button behind a modal dialog does not.

**Looking**

| | |
|---|---|
| `ja2.screen()` | `"MAINMENU_SCREEN"`, `"LAPTOP_SCREEN"`, `"MAP_SCREEN"`, `"GAME_SCREEN"` (tactical), `"MSG_BOX_SCREEN"`, ... |
| `ja2.idle()` | |
| `ja2.ui([{all=true}])` | Clickable elements: `{kind, label, text, name, help, x, y, w, h, enabled, clickable}`. Native ones come first with `kind = "native"`, `id` (their element id), `role` and `focused`. |
| `ja2.texts()` | Visible text: `{text, x, y, w, h}`. |
| `ja2.find(locator)`, `ja2.exists(locator)` | |
| `ja2.pixel(x, y)`, `ja2.pixelIs(x, y, "#rrggbb", [tol])` | |
| `ja2.screenshot(path)` | PNG; relative paths go to `-out`. Returns the full path. |
| `ja2.state()` | `screen, frame, ms, idle, messageBox, messageBoxText, time{day,hour,minute,totalMinutes,totalSeconds,paused,compressed}, money, sector, laptopMode, preBattle{active, sector, enemyCount, canEnter, canRetreat, canAuto}, vehicles[{name,sector,destination,betweenSectors,passengers,helicopter,airborne}], tactical{inCombat,currentTeam,ourTurn,enemyInSector,attackBusy, enemies[{name,class,life,lifeMax,gridNo,dead,level,direction,stance,morale,aimorale,ap,known,los,cover,screenX,screenY}], militia[{name,class,life,lifeMax,gridNo,dead,level,direction,stance,morale,aimorale,ap}], civilians[{name,profile,life,gridNo,dead}]}, mercs[{name,profile,sector,assignment,assignmentName,life,lifeMax,energy,energyMax,betweenSectors,path,vehicle,inSector,gridNo,finalDestination,level,direction,stance,morale,ap,maxAp,screenX,screenY}]` |
| `ja2.battleReport()` | The battle in the loaded sector as data (`nil` until one has started): `started, finished, sector, outcome` (`player`/`enemy`/`draw`/`unresolved`), `endedBy` (`wiped_out`/`lull`/`in_progress`), `rounds, seconds, contactSeconds, firstCasualtySeconds, firstBreakSeconds, disengageSeconds`, `objective{grid,side,held}`, and per-side `player`/`enemy` tallies `{soldiers,alive,dead,wounded,lifeStart,lifeEnd,shots,hits,impacts,damageDealt,damageTaken,breaks}` — see [plan/ai-evaluation.md](plan/ai-evaluation.md). |
| `ja2.gridPos(gridNo, [level])` | Screen point to click to hit that tactical tile. |
| `ja2.gridAt(x, y)` | Tactical tile under a screen point (or -1). |
| `ja2.los(fromGrid, toGrid, [fromLevel], [toLevel])` | Is there an unobstructed line of sight between two tiles? A wall or a closed door blocks it; independent of distance and of who is looking. |

**Game**

| | |
|---|---|
| `ja2.saves()` | Loadable save names. |
| `ja2.load(name, [timeout])` | Loads through the game's own load path (main menu or in game) and waits until done. |
| `ja2.save(name, [description])` | From the map screen or tactical. |
| `ja2.quit()` | |
| `ja2.game.npcs()` | The native people registry ([plan/living-world-npc.md](plan/living-world-npc.md)): `{id, name, kind, homeSectors, placedAtStart, quests[{id, name, role}]}` per named character. |
| `ja2.game.quests()` | The native quest registry: `{id, name, title, status, selfResolving, givers[], resolvers[], dialogue[], prerequisites[], reward, reputationHook, deedHook, note}` per quest. `status` is the live `gubQuest` value (`NOT_STARTED`, `IN_PROGRESS`, `DONE`), so a script can assert a transition: `ja2ctl eval 'return ja2.game.quests()'`. |

**Checks**

| | |
|---|---|
| `ja2.assertInsideScreen()` | Fails (exit 1) if any mouse region or button lies outside the screen, or the native layout audit finds a problem (see *Native UI*). `ja2.assertLayout()` is the same; `ja2.layoutProblems()` lists them. |
| `ja2.setVideo{res="1280x720", uiscale=1, worldzoom=0, window=, filter=}` | Changes the video settings at runtime like the Video options do (the current screen is built again); returns the new `screenSize()` plus `uiScale`, `worldZoom`, `layered`. Headless: `res` is the canvas, and the UI scale only applies with a `worldzoom` other than the UI scale. Nothing is written to ja2.json. |
| `ja2.screenSize()` | `{w, h, stdX, stdY}`: screen size and where the classic 640x480 area starts. |
| `ja2.mapSector(x, y)` | `{x, y, w, h, cx, cy}`: where the strategic map shows sector (x, y) (1..16, A = 1), in screen pixels. The map is scaled on big screens, so use this rather than fixed coordinates. |
| `ja2.expect(value, message)` | Fails the script (exit 1) unless value is truthy. |
| `ja2.check(value, message)` | Records a failure and carries on; the script exits 1 at the end. |
| `ja2.log(...)` | To stderr and the log. |
| `ja2.args` | Values of `-arg`. |
| `ja2.worldEquivalence([dir], [{gpu=true}])` | Phase 8: the current view by the software renderer, the recorded pipeline on the CPU and on the GPU; pixel diffs, op counts, timings, PNGs to `dir`. |
| `ja2.setWorldRenderer(name)` / `ja2.worldRenderer()` | Switch the world renderer (`software`, `gpu`, `pipeline`; the latter two need the world as a layer: `--worldzoom`, or start with `JA2_WORLD_RENDERER`) / what is active and last-frame stats. |
| `ja2.pick()` | What the mouse picks in tactical: `{grid, interactive, target}`. |
| `ja2.debug("light", level, nightLights)`, `("item", grid, item)`, `("corpse", grid, dir)`, `("roof", grid)` | Renderer test scenes: ambient light, an item or a corpse on the ground, a room's roof taken off. |
| `ja2.debug("doormenu", [grid])`, `("pickupmenu")` | Tactical menus directly: the door menu (the nearest door, or the one at `grid`) and the pick-up menu on a small pile of tools at the merc's feet. |
| `ja2.debug("battle", spec)` | Stage a tactical fight in the loaded sector — enemies, gear, skill points, placement, combat — see [plan/e2e-tactical-battles.md](plan/e2e-tactical-battles.md). Fields: `enemies` (count or `{count,class,weapon,distance,grids,units}`), `class`, `weapon`, `enemy_weapon`, `armour`, `distance`, `clear`, `start`, `militia` (a count, or `{count,class,grids,units}`: the player's own AI soldiers, `class` `"green"`/`"regular"`/`"elite"`, staged on their own grids and replacing the sector's militia), `objective` (`{grid, side = "player"|"enemy"}`: the tile the battle is fought over, for `ja2.battleReport()`), and `our` (per-merc `{name,weapon,armour,grid,direction,items,stats,attachments,lbe,pockets,ammo,condition}`). `units` is a list `{{grid,class,weapon,direction}, ...}` for pin-point, staggered placements; it overrides `count`/`grids`. |
| `ja2.debug("fire", gridNo)` | Order the selected merc to shoot at a tile through the real fire-weapon event. |
| `ja2.debug("camera", gridNo)` | Center the tactical view on a tile (normally it follows the selected merc), so a scenario can watch a fight its merc is not at; the next player turn may slide the view back, so call it right before a screenshot. |
| `ja2.loadout(name)` | The equipment schema a merc carries: `weapon`, `ammo`, `ammoType`, `rounds`, `condition`, `attachments` keyed by slot role, `lbe` keyed by kind, `pockets` keyed by pocket, and `armour` by where it is worn with its condition. |
| `ja2.debug("lane", spec)` | Stage a range lane in the loaded sector: one shooter, one target at an exact distance, a fixed seed, both wearing the loadouts given. Fields: `distance` (default 8, and never inside the messy-death range, where one hit can end a soldier), `seed`, `shooter` and `target` (each a loadout table — `name`, `weapon`, `ammo`, `condition`, `attachments`, `lbe`, `pockets`, `armour`, `stats`). The target is a dummy: full health, its armour restored before every shot. See [tests/e2e/README.md](../tests/e2e/README.md). |
| `ja2.debug("laneShot")` | Order one shot down the lane, against the dummy restored to full health and full-condition armour. Returns true when ordered; the reason a refusal happened is in `ja2.lane().refusal`. |
| `ja2.lane()` | The lane and every shot fired down it: the geometry (`shooter`, `target`, `distance`, `targetGrid`, `weapon`, `ammo`, `ammoType`, `condition`, `rounds`, `targetArmour`) and `shots`, one entry per trigger pull with `chanceToHit`, `roll`, `hit`, `impacted`, `noiseVolume`, `conditionBefore`, `conditionAfter`, `wear`, `distance`, `hitLocation`, `impactBeforeArmour`, `armourProtection`, `damage` and `penetrated`, plus `summary` (`shots`, `hits`, `impacts`, `damage`, `protection`, `penetrated`, `noise`). |
| `ja2.debug("campaign", spec)` | Author a whole campaign state on the live globals (money, clock, difficulty, towns, sectors, roster, gear, quests/facts), so a save taken after staging is a real save — see [plan/e2e-campaign-state.md](plan/e2e-campaign-state.md). Per merc: `sector`, `assignment` (`"squad"`, `"squad_2"`, `"doctor"`, `"patient"`, `"repair"`, `"train_self"`, ...), `contract_days_left`, `weapon`, `armour`, `health`, `life` (a wound, below the max), `energy` (current and max breath, for a tired merc), `hold` (an item into his hand) and `items` (names, or `{item, count, condition}` tables). Staging also rebases the strategic event queue on the staged clock, so the missed hours between the new game and the staged day do not fire at once. |
| `ja2.cursor()` | The tactical cursor as data (#318): `shown`, `mode` (`move`, `target`, `item`, ...), `shape`, `tone`, `marker`, `ap` / `apLeft`, `hit`, `aim`, `why` (the reason when the tone is not ok: `no_ap`, `no_path`, `next_turn`, ...), `target`, `tile`, `id` (the legacy `UICursorID`), `lines` (the chip), `path` (`steps`, `solid`, `total`, `now`, `next`, `beyond`), `markerAt` and `destAt` in world pixels. Hover with `ja2.move(x, y)` and wait a second for the path. |
| `ja2.inventory([merc])` | The inventory core as data: `hand` (`item`, `name`, `count`, `from`, `fromSlot`, or nil), `asking` (`"merge"` or `"permanent"` while a move waits for an answer), `last` (the last move: `ok`, `action`, `why`, `item`, `apFrom`, `apTo`), the merc's `pockets` keyed by slot and the open `sheet`. |
| `ja2.inventoryOp(op, spec)` | One inventory move, returning its outcome. `"click"` `{merc, slot, right, ctrl}` on a pocket, `"answer"` `{yes}`, `"attach"` `{index, right}`, `"unload"`, `"money"` `{which, right}`, `"close"`, `"move"` `{merc, from, to}` (one merc, no cursor), `"plan_card"` `{to}` (what dropping the hand on that merc's card would do: `{ok, action = give|feet|none, why, apFrom, apTo, tiles}`), `"drop_card"` `{to}` (does it), `"plan_slot"` `{merc, slot}` (the verdict of a drop on a pocket, without the click). |
| `ja2.debug("hand", spec)` | Hold `spec.item` (internal name, `count`) in `spec.merc`'s hand, as a click on a pocket would have. |
| `ja2.campaign()` | Read the campaign state back: `day, hour, minute, money, difficulty, towns, sectors, mercs, quests, facts`. |
| `ja2.debug("aimavailable", [name])` | Make A.I.M. mercs contactable again, undoing the "on assignment" roll a new game makes (chance 5 * experience level). `name` limits it to one merc's nickname; without one the whole A.I.M./M.E.R.C. roster is cleared. A script that hires through the laptop needs this: an on-assignment merc shows a status badge instead of the member page's Contact button. |
| `ja2.debug("entersector", spec)` | Move the team into `spec.sector` and load it in tactical (`clear_enemies`, optional `battle`); the caller waits for `GAME_SCREEN`. |
| `ja2.debug("transition", t)` | Draw one frame of the sector-load transition at progress `t` (0..1): it blocks the game loop and is only drawn where someone can see it, so this is how a tour looks at it. |
| `ja2.debug("npcs", spec)` | Spawn townsfolk (`count`) or named NPCs (`profiles = {"TONY"}`) in the loaded sector; `ja2.state().tactical.civilians` lists them. |

Timeouts report the screen and, if one is open, the message box text, e.g.
`timed out after 120000 ms waiting for screen MAP_SCREEN (screen:
MSG_BOX_SCREEN); a message box is open: "Surrender? YES NO"`.

## Native UI

Screens and overlays redesigned for [native-modern-game.md](plan/native-modern-game.md) run on RmlUi over the legacy
game (`src/game/NativeUI/`). Which UI a screen uses is its `ui_mode`, resolved when the screen is entered:
`ja2.setUiMode(key)` override > `"ui_mode"` in `ja2.json` > the default. Keys: `credits`, `mainmenu`, `options`, `saveload`,
`newgame`, `loadscreen`, `mapscreen`, `autoresolve`, `shopkeeper`, `tactical` (default native), `msgbox`, `tooltip`, `toasts`, `cursor` (default legacy). The native UI needs a 1280x720 output: below that everything is legacy.

Headless (and in every automation session) the native layer is drawn in software and blended into the frame, so
screenshots, `ja2.pixel` and goldens include it. Coordinates are the same canvas pixels as everything else.

| | |
|---|---|
| `ja2.setUiMode(key, "legacy" \| "native" \| "default")` | Override a screen's ui_mode for this session (applies at the next entry). |
| `ja2.uiMode(key)` | `{configured, resolved, reason}`. |
| `ja2.nativeUi()` | `{running, renderer, w, h, dp, uiScale, screen, documents, warnings, capturesMouse, focused, modes}`. |
| `ja2.setUiScale(s)` | The native UI scale (1 = 100 %, up to 3). |
| `ja2.focus(id)` | Give an element keyboard focus (with the focus ring). Tab/arrows/Enter work through `ja2.key`. |
| `ja2.viewModel(name)` | A view model's fields as a table: an open screen's (`"credits"`, `"options"`, ...) or one made for the call, which reads the game now (`"status"`: day, time, money, sector, mercs; `"options"`: the game settings, volumes, video settings and this campaign's options; `"mainmenu"`, `"saveload"`: the saves). |
| `ja2.viewModelCommand(name, command, ...)` | Run a view model command, like a click on its button would. |
| `ja2.toast(text, [kind])` | Show a toast (`info`, `ok`, `warn`, `danger`). |
| `ja2.debug("mock", "phase3/mainmenu")` | Show a static design mock (`assets/ui/mocks/<name>.rml`) through the native runtime until Esc. |
| `ja2.debug("shopkeeper", [dealerId])` | Open the arms-dealer trade screen (`SHOPKEEPER_SCREEN`) with a dealer (default Tony), spawning the dealer next to the selected merc if he is not in the sector. |
| `ja2.debug("msgbox", text, [kind])` | Open a message box from the current screen (`ok`, `yesno`, `yesnolie`, `okskip`, `four`); `ja2.lastMessageBoxResult()` is what it returned. |

**Element ids.** Every element with an `id` in a native document is addressable: `ja2.click{id="credits.back"}`,
`ja2.find{id="msgbox.yes"}`, `ja2ctl click '#credits.back'`. Ids are stable English names with dots
(`screen.part[index].name`). The label is the element's `data-label`, else its text, else its `title` (tooltip).

**Layout audit** (`ja2.layoutProblems()`, part of `assertInsideScreen`): for every visible native element, nothing
lies off the output unless a clipping container hides it, nothing is cut sideways by a clipping container
(scroll areas may cut vertically), no text is wider than its box (truncated) unless the element has a `title`, and no
two interactive elements overlap. Mark decorative or deliberately clipped parts `class="audit-skip"`.

**Idle.** Native screens have no loading states of their own; a scrolling screen (credits) counts as idle, like its
legacy version.

## The server protocol

`ja2 -serve PORT` accepts one JSON object per line on `127.0.0.1:PORT` and
answers with one line each. Connections may be opened and closed freely:

```json
{"id": 1, "lua": "return ja2.screen()"}
{"id": 2, "call": "click", "args": [{"text": "Ok", "exact": true}]}
{"id": 3, "call": "shutdown"}
```

```json
{"id": 1, "ok": true, "result": "MAINMENU_SCREEN", "screen": "MAINMENU_SCREEN", "frame": 45, "ms": 766, "idle": true}
{"id": 2, "ok": false, "error": "timed out after 10000 ms waiting for exact \"Ok\" ...", "kind": "timeout", ...}
```

`call` invokes `ja2.<name>(args...)`. `kind` is one of `script`,
`expectation`, `timeout`, `crash`, `exited`. The game does not advance between
requests.

## Tests

End-to-end tests live in [`tests/e2e/`](../tests/e2e/README.md) and run
through CTest (they need the game data and the Python environment from
`uv sync`):

```bash
ctest -L e2e -j8 --output-on-failure      # in the build directory
```

## Making more of the UI addressable

When a widget's caption is part of its image, name it where it is created:

```cpp
GUIButtonRef const b = QuickCreateButton(img, x, y, MSYS_PRIORITY_HIGHEST, MenuButtonCallback);
b->SetName("New Game");           // or region.SetName(...) for a MOUSE_REGION
```

The name is never displayed. Use a stable English name, not a translated
string. When a screen has a state that input should wait out (an animation,
a fake loading delay), expose it as a predicate (`LaptopIsBusy()`,
`MainMenuIsReady()`, `HeliDropInProgress()`) and check it in
`NothingInFlight()` in `src/game/Automation/AutomationSession.cc`.
