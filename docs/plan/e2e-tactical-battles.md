# E2E tactical battles — design

> **Status: first slice implemented.** The plan below is the contract for the track; the
> first tests built on it are:
>
> - `tests/e2e/battle_smoke.lua` — three mercs against ten enemies in the landed sector,
>   driven through the native tactical HUD.
> - `tests/e2e/lib/battle.lua` — the reusable fixtures and orders the scenarios share.
> - `ja2.debug("battle", spec)` / `ja2.debug("fire", gridNo)` — the C++ harness in
>   [`src/game/Automation/BattleScenario.cc`](../../src/game/Automation/BattleScenario.cc).
>
> Still to come: the scenario corpus in CI (#65), and assertions for line of sight, cover
> and breaks (#64).

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
2. **Equip**: every merc in the sector gets the requested gun (loaded) and spare
   magazines, and optionally a kevlar vest, helmet and leggings.
3. **Spawn**: `TacticalCreateEnemySoldier(class)` for each enemy, placed on free tiles a
   given distance from the team's centre of mass, facing the team. Each enemy keeps the
   kit the generator gave it, unless `enemy_weapon` overrides it.
4. **Sighting**: `AllTeamsLookForAll(TRUE)` settles who sees whom.
5. **Combat**: `EnterCombatMode(OUR_TEAM)` starts turn-based combat with the player's turn,
   so a scenario can act immediately.

The spec table:

| Field | Default | Meaning |
|---|---|---|
| `enemies` | `10` | how many enemies to spawn |
| `class` | `"administrator"` | `"administrator"`, `"army"` or `"elite"` |
| `weapon` | `"MP5K"` | internal name (`weapons.json`) of the gun for our mercs |
| `enemy_weapon` | *(generated)* | internal name of a gun to give every enemy |
| `armour` | `true` | give our mercs a kevlar vest, helmet and leggings |
| `distance` | `6` | tiles from the team to stand at |
| `clear` | `true` | clear the sector's existing enemies first |
| `start` | `true` | enter turn-based combat when staged |

Item names are the original internal names, looked up through
`GCM->getItemByName()`; an unknown name fails the scenario with the name in the message.

### Reading the fight

`ja2.state().tactical` gained `ourTurn` and `enemies`: for every enemy in the sector, its
`name`, `class`, `life`, `lifeMax`, `gridNo`, `dead` and (in tactical) `screenX`/`screenY`.
Mercs already expose `life`, `lifeMax`, `inSector` and `gridNo`. Together with the native
tactical view model (`ja2.viewModel("tactical")`: `combat`, `our_turn`, `can_end`,
`cards[i].{name,ap,hp,en,mo,ammo,sel,done}`, `lines`, `log`) a scenario can assert both
game state and what the HUD shows.

### Orders — `ja2.debug("fire", gridNo)`

The selected merc is ordered to shoot at a tile through the real fire-weapon event
(`SendBeginFireWeaponEvent`), the same call the UI makes when a shot is clicked. AP, ammo,
jams, turning, bullets and death all run normally. `ja2.waitIdle()` then waits the shot
out, because `NothingInFlight()` already treats `ubAttackBusyCount` as busy.

`ja2.state().tactical.attackBusy` exposes the attack-busy count that `waitIdle()` watches,
and each merc carries `finalDestination`. A merc who dies is removed from the map
(`gridNo` becomes `NOWHERE`) but keeps the tile he fell on, so `NothingInFlight()` no
longer counts a dead merc as still walking — otherwise a single casualty left the game
"busy" forever.

The scenario pieces are small Lua steps (`tests/e2e/lib/battle.lua`):

- `battle.stage(spec)` — stage a fight and return the tactical state.
- `battle.enemies()` / `battle.mercs()` — the living soldiers, state order.
- `battle.select(i)` / `battle.card(i)` / `battle.selected()` — drive and read the native
  squad bar.
- `battle.nearest(i)` — the enemy nearest to merc `i`.
- `battle.fireAt(e)` — shoot enemy `e`, returning whether it was hit.
- `battle.endTurn()` — press the native End Turn button and wait out the enemy turn.
- `battle.playTurn()` — each merc shoots the nearest enemy while he has AP, then End Turn.

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

The first scenario asserts the shape of a fight: three mercs armed and with AP on the
native bar; ten enemies staged; the mercs fire; ammo changes or an enemy falls; the enemy
life total drops; the log records combat; the fight costs game time. Line of sight, cover
choice, AP accounting and morale breaks are #64.

## Running

```bash
ctest -R e2e_battle -V --output-on-failure                 # from the build directory
python tools/ja2ctl.py run tests/e2e/battle_smoke.lua --isolated --res 1920x1080
python tools/ja2ctl.py run tests/e2e/battle_smoke.lua --isolated --res 1920x1080 --show
```

Battle scenarios run at 1920x1080: the native tactical HUD needs 1280x720, and the generic
640x480 loop and the resolution matrix skip `battle_*` scripts.

## Roadmap

| Issue | Slice |
|---|---|
| #61 | this plan doc |
| #62 | scenario fixtures (`lib/battle.lua`) — **first cut landed here** |
| #63 | deterministic harness (`ja2.debug("battle"/"fire")`) — **first cut landed here** |
| #64 | assertions: LOS, cover, positioning, AP, morale and breaks, outcome |
| #65 | the scenario corpus wired into `ctest -L e2e` and run on every PR |
