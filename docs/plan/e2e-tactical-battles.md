# E2E tactical battles — design

> **Status: assertions implemented.** The plan below is the contract for the track; the
> tests built on it are:
>
> - `tests/e2e/battle_smoke.lua` — three mercs, decked out with a chosen weapon, armour,
>   items and skill points, against ten enemies in the landed sector, driven through the
>   native tactical HUD to a win. It also checks that firing spends AP.
> - `tests/e2e/battle_los.lua` — line of sight, cover and positioning: a pin-pointed,
>   staggered enemy line the team can partly see and shoot; asserts what each side can see,
>   the cover the building gives, that firing spends AP, whose morale moves, the casualties
>   and the state the fight is left in (#64).
> - `tests/e2e/battle_militia.lua` — the AI battle: twenty player militia against ten
>   low-level enemy soldiers on a rural map, both sides staged on predetermined tiles.
>   The militia fight on their own AI turn, so the scenario ends the player's turns and
>   asserts the militia won (#65).
> - `tests/e2e/lib/battle.lua` — the reusable fixtures and orders the scenarios share.
> - `ja2.debug("battle", spec)` / `ja2.debug("fire", gridNo)` — the C++ harness in
>   [`src/game/Automation/BattleScenario.cc`](../../src/game/Automation/BattleScenario.cc).

## Goal

Simulate a whole firefight headless and assert what happened, so a change to the tactical
AI, the combat maths or the HUD shows up as a failing battle rather than as a feeling:

- A scenario places squads, picks weapons, armour and stances, issues orders and runs the
  turns — all through the game's own soldier, combat and UI code.
- The outcome is deterministic: the same scenario and seed produce the same dead and the
  same damage, every time.
- Each scenario is a small, bounded run: seconds of wall-clock time in CI, no rendering.

This is the tactical half of the "Better E2E" milestone; the campaign track
(`docs/plan/`) covers the strategic arc.

## Non-goals

- **Not a replacement for the unit tests.** These are end-to-end: real map, real combat
  pipeline, real HUD. A rule that can be tested in a C++ unit test stays there.
- **No pixel goldens for the battle itself.** Combat is non-deterministic in pixels
  (animations, faces, particles). The scenarios assert *state* and *HUD model* values and
  take ordinary screenshots for review, not golden comparisons.
- **No new gameplay.** The harness stages fights through existing functions
  (`TacticalCreateEnemySoldier`, `SendBeginFireWeaponEvent`, `EnterCombatMode`); it does
  not add a parallel combat path.

## The harness

### Staging — `ja2.debug("battle", spec)`

`StageBattle()` (`src/game/Automation/BattleScenario.cc`) runs on the tactical screen in
the loaded sector:

1. **Clear** (default): remove the enemies already in the sector, as
   `ja2.debug("clearenemies")` does.
2. **Equip**: every merc in the sector gets a gun (loaded) with spare magazines, body
   armour, extra items and chosen skill points — per merc via `our`, or the spec defaults.
3. **Place**: mercs with an `our[i].grid` are moved there; enemies are placed on their
   `grids`, on the `units` list's own grids, or, without either, on free tiles a given
   distance from the team's centre of mass. `direction` (0..7) sets the facing.
   Placement does not run sight, so it cannot start combat before the first turn is chosen.
4. **Spawn**: `TacticalCreateEnemySoldier(class)` for each enemy. Each keeps the kit the
   generator gave it, unless `enemy_weapon` (or a `units` entry's `weapon`) overrides it.
5. **Combat**: `EnterCombatMode(OUR_TEAM)` starts turn-based combat with the player's turn,
   then `AllTeamsLookForAll(FALSE)` settles who sees whom, so a scenario can act immediately.

The spec table:

| Field | Default | Meaning |
|---|---|---|
| `enemies` | `10` | how many enemies to spawn, or a table `{ count, class, weapon, distance, grids = { grid, ... }, units = { { grid, class, weapon, direction }, ... } }` |
| `class` | `"administrator"` | `"administrator"`, `"army"` or `"elite"` |
| `weapon` | `"MP5K"` | internal name (`weapons.json`) of our mercs' gun |
| `enemy_weapon` | *(generated)* | internal name of a gun to give every enemy |
| `armour` | `"kevlar"` | `true`/`false`, `"kevlar"` or `"spectra"` for our mercs |
| `distance` | `6` | tiles from the team to stand at |
| `clear` | `true` | clear the sector's existing enemies first |
| `start` | `true` | enter turn-based combat when staged |
| `militia` | — | the player's own AI soldiers (`MILITIA_TEAM`): a count, or a table `{ count, class, grids = { grid, ... }, units = { { grid, class, direction }, ... } }`. `class` is `"green"` (default), `"regular"` or `"elite"`; `units` pins each militia to its own grid. The staged militia replace the sector's own, so the spec's list is the whole force. |
| `our` | — | per-merc setup, matched by `name` (or by position when unnamed) |

`enemies.units` spawns exactly those enemies, each on its own grid with its own class,
gun and facing; it overrides `count`/`grids` and is how a scenario takes pin-point
control of a staggered line.

`militia` stages the player's militia: friendly soldiers that fight on the player's side
on their own AI turn (after the enemy's), not under the player's control. A scenario
that stages militia can therefore let the two AIs fight and assert the outcome, without
the player firing a shot; `battle_militia.lua` is that scenario.

Each `our` entry:

| Field | Meaning |
|---|---|
| `name` | which merc (the name shown on the squad bar) |
| `weapon` | internal name of the gun for this merc |
| `armour` | `true`/`false`/`"kevlar"`/`"spectra"` |
| `grid` | exact tile to stand on |
| `direction` | facing, 0..7 (0 = north); omitted leaves the generated facing |
| `items` | array of internal names to give (pockets fill up on their own) |
| `stats` | skill points: `marksmanship`, `agility`, `dexterity`, `strength`, `leadership`, `wisdom`, `medical`, `mechanical`, `explosive`, `morale`, `level` (1..10), `health` (sets life and life max) |

Item names are the original internal names, looked up through
`GCM->getItemByName()`; an unknown name fails the scenario with the name in the message.

### Reading the fight

`ja2.state().tactical` gained `ourTurn`, `attackBusy`, `enemies` and `militia`. For every
enemy in the sector it gives `name`, `class`, `life`, `lifeMax`, `gridNo`, `dead`, `level`,
`direction`, `stance`, `morale`, the AI's own `aimorale` verdict (0 = hopeless .. 4 =
fearless, what the AI reads as whether it is close to breaking) and `ap`. Two fields
describe vision: `known` (the player knows about him, so he is rendered) and `los` (some
merc can currently trace an unobstructed line of sight to him within sight range), plus
`cover` (the chance a shot from the nearest merc has to get through; lower is more cover).
In tactical it also has `screenX`/`screenY`. `militia` carries the same shape for the
player's own AI soldiers (`name`, `class`, `life`, `lifeMax`, `gridNo`, `dead`, `level`,
`direction`, `stance`, `morale`, `aimorale`, `ap`), so an AI battle can assert what
happened to them.

Mercs gained `level`, `direction`, `stance`, `morale`, `ap` and `maxAp` on top of `life`,
`lifeMax`, `inSector`, `gridNo` and `finalDestination`. `ja2.los(fromGrid, toGrid)`
answers the tile-to-tile question directly (a wall or closed door blocks it), independent
of who is looking. Together with the native tactical view model
(`ja2.viewModel("tactical")`: `combat`, `our_turn`, `can_end`,
`cards[i].{name,ap,hp,en,mo,ammo,sel,done}`, `lines`, `log`) a scenario can assert both
game state and what the HUD shows.

### Orders — `ja2.debug("fire", gridNo)`

The selected merc is ordered to shoot at a tile through `HandleItem` — the same entry the
AI fires through (`AIMain.cc`) and the UI reaches when a shot is clicked. AP, ammo, jams,
turning, bullets and death all run normally. `ja2.waitIdle()` waits the attack out, but the
attack-busy count can clear while the bullet is still in the air, so `battle.waitShot()`
also allows the flight time before the result is read back. Ordering a shot at a target off
the merc's facing can merely turn him, so the helper orders it again once he faces it.

`ja2.state().tactical.attackBusy` exposes the attack-busy count that `waitIdle()` watches,
and each merc carries `finalDestination`. A merc who dies is removed from the map
(`gridNo` becomes `NOWHERE`) but keeps the tile he fell on, so `NothingInFlight()` no
longer counts a dead merc as still walking — otherwise a single casualty left the game
"busy" forever.

The scenario pieces are small Lua steps (`tests/e2e/lib/battle.lua`):

- `battle.stage(spec)` — stage a fight and return the tactical state.
- `battle.enemies()` / `battle.mercs()` / `battle.militia()` — the living soldiers, state
  order; `battle.byGrid(grid)`, `battle.militiaByGrid(grid)` and `battle.merc(name)` look
  one up by tile or name.
- `battle.inSight()` — the living enemies the team currently has a line of sight to.
- `battle.select(i)` / `battle.card(i)` / `battle.selected()` — drive and read the native
  squad bar.
- `battle.nearest(i)` — the enemy nearest to merc `i`, preferring one he can see.
- `battle.fireAt(index)` / `battle.fireAtGrid(grid)` — shoot an enemy or a tile, returning
  whether the order fired and whether it hit.
- `battle.endTurn()` — press the native End Turn button and wait out the enemy turn.
- `battle.playTurn()` — each merc shoots the nearest enemy while he has AP, then End Turn.
- `battle.settle()` / `battle.waitShot()` — wait the game (or a shot) out, declining the
  in-combat surrender and first-aid prompts so a scenario is not stopped by a modal box.

## Determinism and runtime

- The RNG is seeded once per process (`-seed`, default `1`) before the game initialises
  (`src/sgp/SGP.cc`), and the automation clock is virtual, so a scenario is reproducible.
  `tests/e2e/check_determinism.py` already proves the same script produces the same frames.
- One scenario per process (`ja2ctl run ... --isolated`), so nothing leaks between them.
- Scenarios are **bounded**: a fixed number of rounds, an AP-gated number of shots per
  merc, and a wall-clock `--timeout`. A scenario that needs to prove a battle ends asserts
  the end state; it never waits for one.
- The harness itself does no rendering; `ja2.waitIdle()` between orders keeps the run to
  seconds even though the game steps every frame.

## Assertions

A scenario asserts from three sources, cheapest first:

1. **Game state** (`ja2.state()`): casualties on both sides, life totals, AP, in-combat and
   current team, game time.
2. **The HUD model** (`ja2.viewModel("tactical")`): the squad cards (name, AP, HP, morale,
   ammo, selected/done), the combat flag and turn, the message log (`cls == "combat"`).
3. **Screenshots** for review (`shots.take`), never as the pass/fail signal.

`battle_smoke.lua` asserts the shape of a fight *and* its outcome: three mercs, decked out
with a G11, spectra armour, a medkit and top skill points, on the native bar at 100 health;
ten enemies staged; the mercs fire; a shot spends AP; ammo changes; the log records the
fight; and after a few turns the enemies are wiped out, combat is over and all three mercs
are still standing.

`battle_los.lua` is the #64 assertion set. Three mercs face a pin-pointed, staggered enemy
line: three enemies in the open that the team can see, and two behind the east building that
it cannot. It asserts the staged positions, line of sight (both per enemy via `los` and per
tile via `ja2.los`), the cover the building gives (`cover`), that a shot spends AP, that the
exposed line takes the casualties while the hidden line is untouched and still unseen, that
the team's morale rises as it wins (with every enemy's AI morale verdict exposed), and the
state the fight is left in — combat continues around the squad the team cannot reach. A
forced HOPELESS break (morale is the input to the AI's flee/break decision) needs a way to
stage a spent/outmatched enemy; `aimorale` is exposed for it.

`battle_militia.lua` is the AI-vs-AI scenario (#65): twenty green militia against ten
administrators on E11, a rural sector (woods, grass and a river). Both forces are pinned to
exact tiles — two militia ranks facing south, the enemy patrol a dozen tiles south of them,
and one player merc behind the line who never fires. It asserts the staging (20 militia, 10
enemies, every one on its tile, the lines in sight of each other) and then lets the two AIs
fight: the scenario only presses End Turn, waits out the enemy and militia turns, and
asserts the militia won — the enemy patrol wiped out, combat over, the militia and the merc
still standing — bounded at 12 rounds. Determinism is per the usual contract: the same spec
and seed give the same dead (the isolated run reports the same rounds and survivors every
time).

## Running

```bash
ctest -R e2e_battle -V --output-on-failure                 # from the build directory
python tools/ja2ctl.py run tests/e2e/battle_smoke.lua --isolated --res 1920x1080
python tools/ja2ctl.py run tests/e2e/battle_los.lua --isolated --res 1920x1080
python tools/ja2ctl.py run tests/e2e/battle_militia.lua --isolated --res 1920x1080
python tools/ja2ctl.py run tests/e2e/battle_los.lua --isolated --res 1920x1080 --show
```

Battle scenarios run at 1920x1080: the native tactical HUD needs 1280x720, and the generic
640x480 loop and the resolution matrix skip `battle_*` scripts. `ctest` registers every
`battle_*.lua` as `e2e_<name>` (the glob in `CMakeLists.txt`), so a new scenario joins the
corpus by existing. Running the e2e suite on every PR needs the synthetic game data of
#302; until then the corpus runs wherever the real game data is.

## Roadmap

| Issue | Slice |
|---|---|
| #61 | this plan doc |
| #62 | scenario fixtures (`lib/battle.lua`) — **first cut landed here** |
| #63 | deterministic harness (`ja2.debug("battle"/"fire")`) — **first cut landed here** |
| #64 | assertions: LOS, cover, positioning, AP, morale and breaks, outcome — **landed here** (`battle_los.lua`) |
| #65 | the scenario corpus wired into `ctest -L e2e` — **landed here** (`battle_militia.lua` and the AI battle harness); running it on every PR is #302 |
