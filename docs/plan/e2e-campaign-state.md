# E2E campaign state — design

> **Status: two slices implemented.** The plan below is the contract for the track; the
> tests built on it are:
>
> - `tests/e2e/campaign_state.lua` — author a non-default campaign state, assert it through
>   `ja2.campaign()` and the status view model, save, reload and assert it again.
> - `tests/e2e/battle_campaign.lua` — author a state, walk into a controlled town with
>   townsfolk, then step into a tactical battle in the loaded sector.
> - `tests/e2e/world_map_steps.lua` — the world-map steps: plot a path, move a squad
>   between sectors, arrive, trigger an encounter and retreat from it, and board, fly and
>   leave the helicopter, asserted through `ja2.state()` and the native map screen's
>   element ids (issue [#69](https://github.com/adhikasp/ja2-pecel-bakwan/issues/69)).
> - `tests/e2e/world_map_actions.lua` — the strategic actions around the map: split a
>   squad, rest, doctor/patient, repair, train, and move an item between two mercs through
>   the sector inventory, driven by the native element ids.
> - `tests/e2e/lib/campaign.lua` — the shared steps (`stage`, `at`, `assertState`,
>   `enterSector`, `enterTown`, `stepIntoBattle`).
> - `tests/e2e/lib/worldmap.lua` — the world-map steps (`staged`, `plot`, `travel`,
>   `waitLanded`, `assign`, `resume`, `runHours`).
> - `ja2.debug("campaign", spec)`, `ja2.campaign()`, `ja2.debug("entersector", spec)` and
>   `ja2.debug("npcs", spec)` — the C++ harness in
>   [`src/game/Automation/CampaignScenario.cc`](../../src/game/Automation/CampaignScenario.cc).

## Goal

One layer for campaign e2e that lets a test (a) **synthesise any campaign state directly**
and (b) **move between named milestones, asserting the state at each** — so a test says
"day 12, Drassen held at 70 %, 120k funds, four mercs" instead of replaying the UI to get
there.

The tactical track proved the pattern with `BattleScenario.cc` / `ja2.debug("battle", spec)`.
This is the strategic analogue: it mutates the **live** campaign globals (no parallel state),
so a save taken after staging is a real, save-game-compatible save.

## Non-goals

- **Not the rest of the campaign arc.** No hiring flow, no long-run soak, no end-game.
- **No new gameplay and no parallel state.** The harness writes the globals the game already
  reads (`StrategicMap`, `SectorInfo`, `gTownLoyalty`, `gMercProfiles`, `gubQuest`,
  `gubFact`, `LaptopSaveInfo`, `gGameOptions`, the clock).
- **No new save format and no byte-compatibility promise.**
- **Not a "replay history to any day" simulator:** it sets state, it does not replay the
  events that led there.

## Part 1 — state authoring

`StageCampaign()` runs on the live game and applies the spec in a defined order — clock →
money/difficulty → towns → sectors → roster → gear → progress — then reconciles.

| State | Globals | Serialised by |
| --- | --- | --- |
| money | `LaptopSaveInfo.iCurrentBalance` | `SaveLaptopInfoToSavedGame` |
| day / time | `guiGameClock` (drives `guiDay`, `guiHour`, `guiMin`) | `SaveGameClock` |
| difficulty | `gGameOptions.ubDifficultyLevel` (1..3) | `SaveGeneralInfo` / game options |
| sector control / garrison | `StrategicMap[].fEnemyControlled`, `SectorInfo[].ubNumAdmins/Troops/Elites` (+ `*InBattle`) | `SaveStrategicInfoToSavedFile` |
| town loyalty / militia | `gTownLoyalty[].ubRating/fStarted`, `SectorInfo[].ubNumberOfCivsAtLevel[]` | `SaveStrategicTownLoyaltyToSaveGameFile` |
| merc roster | `gMercProfiles[].bMercStatus/sSector`, the live `SOLDIERTYPE` (`sSector`, `bAssignment`, contract) | `SaveMercProfiles`, `SaveSoldierStructure` |
| per-merc inventory | `SOLDIERTYPE.inv[]` (`CreateItems` / `AutoPlaceObject`) | `SaveSoldierStructure` |
| progress | `gubQuest[]`, `gubFact[]` | `SaveQuestInfoToSavedGameFile` |

The spec:

```lua
ja2.debug("campaign", {
  day = 12, hour = 9, minute = 0,       -- world clock (any subset; the rest is kept)
  money = 120000,
  difficulty = 2,                        -- 1..3 or "easy"/"medium"/"hard"
  towns = {                              -- keyed by town id or name
    ["Drassen"] = { owned = true, loyalty = 70, militia = { green = 5, regular = 3, elite = 1 } },
  },
  sectors = {                            -- "A9" etc; overrides town-derived control
    ["A9"] = { enemy = false, admins = 0, troops = 0, elites = 0 },
  },
  mercs = {                              -- roster + gear; item names from weapons.json etc.
    { name = "Barry", sector = "A9", assignment = "squad", contract_days_left = 7,
      weapon = "G11", armour = "spectra", health = 100,
      items = { "FIRSTAIDKIT", "CROWBAR" } },
    { name = "Ivan", sector = "A9", assignment = "patient", health = 100, life = 50,
      energy = 40, hold = "MEDICKIT",     -- a wound, a tired man, a kit in the hand
      items = { { item = "G11", condition = 60 } } },  -- a neglected rifle, for a repair step
    { name = "Trevor", dead = true },    -- did not come home: the profile only (the victory epilogue's fallen)
  },
  kills = { admins = 21, troops = 402, elites = 189, player = 300 },  -- gStrategicStatus's kill counters
  progress = {
    quests = { HELD_IN_ALMA = "done", KINGPIN_MONEY = "in_progress" },
    facts  = { FACT_PABLO_PUNISHED_BY_PLAYER = true },
  },
  save = "my-fixture",                   -- optional: the Lua helper saves the staged state
})
```

Semantics:

- Applied in a defined order and **reconciled** afterwards: `fEnemyControlled` and the
  `ubNum*` / `ubNum*InBattle` counters agree (an owned sector keeps no garrison unless the
  spec says so), owned towns have their sectors friendly, loyalty is in 0..100, and each
  live merc's `gMercProfiles` entry follows the soldier.
- Item and skill helpers are **shared with `BattleScenario`** —
  `src/game/Automation/ScenarioItems.{h,cc}` holds `ItemByName`, `GiveGun`, `GiveItem`,
  `EquipArmour` and `ApplyStats` so both harnesses use one implementation.
- Unknown town / sector / merc / quest / fact / item names throw `std::runtime_error` with
  the name. A spec never half-applies silently.
- Staging *replaces* the named dimensions, it does not add to them.
- A merc who is not on the team yet is created from his profile (as `HireMerc` does), so
  `mercs` can build a roster from nothing.
- Per-merc `life` is a wound below `health` (the max); `energy` sets both current and max
  breath, so a merc is tired enough to sleep; `hold` puts an item in his hand, where a
  doctor or repairman step needs its kit; an `items` entry may be a table
  (`{ item, count, condition }`) for a stack or a neglected item.
- Staging also **rebases the strategic event queue on the staged clock**: a new game starts
  on day 1, and every event scheduled before the staged "now" would otherwise fire at once
  when the clock next runs — replaying days of missed hourly updates, each of which
  fatigues every merc to the floor. Periodic and daily events keep their cadence from the
  staged now; one-shot events that were missed are dropped. The spec states where the
  campaign is, it does not replay how it got there.

### Reading it back

`ja2.campaign()` returns `day`, `hour`, `minute`, `money`, `difficulty`, `towns` (id, name,
owned, loyalty, started, militia, sectors), `sectors` (every `A9`..`P16`: `enemy`, `admins`,
`troops`, `elites`), `mercs` (name, profile, sector, assignment, life, `contractDaysLeft`,
`items`), `quests` and `facts`. The status view model (`ja2.viewModel("status")`) shows the
same clock, money and roster on screen.

## Part 2 — the step framework

`tests/e2e/lib/campaign.lua`:

- `campaign.stage(spec)` — apply a state via `ja2.debug("campaign", spec)` and return
  `ja2.campaign()`.
- `campaign.at(spec)` — stage, then `ja2.save` + `ja2.load` so a test starts from a real
  save (the checkpoint helper).
- `campaign.assertState{ ... }` — named-milestone assertions against `ja2.campaign()`.
- `campaign.toMap()` — leave the laptop for the map screen.
- `campaign.enterSector("A9" | spec)` — move the team into a sector and load it in tactical.
- `campaign.enterTown(name, { npcs = n })` — take a town and walk into one of its sectors
  with townsfolk.
- `campaign.stepIntoBattle(spec)` — walk into a sector and stage a fight there.

### World-map steps

`ja2.debug("entersector", spec)` moves the whole team into `spec.sector`, clears the
garrison when a fight is staged, and calls `SetCurrentWorldSector`; the screen change is
pending, so the Lua helper waits for `GAME_SCREEN`. `ja2.debug("npcs", spec)` spawns
townsfolk (generic civilians) or named NPCs near the team once the sector is loaded.
`ja2.state().tactical.civilians` lists them so a test can assert who is there.

The map screen itself is driven through its element ids by `tests/e2e/lib/worldmap.lua`:
`staged(spec)` lands a staged campaign on the map screen (the staged clock makes the map
take input without the landing flow), `plot(name, sector)` plots and confirms a route,
`waitLanded` runs the clock until the squad arrives, `assign(name, ...)` walks the
assignment menus, `resume` keeps the clock at full compression (the map starts paused and
events stop time), and `runHours(h)` runs the clock for h game hours, dismissing the boxes
that pop up on the way.

`ja2.state()` grew the readings those steps assert on:

- `mercs[]`: `betweenSectors` (a strategic move is in progress), `path` (the plotted route
  as sector codes), `energy` / `energyMax` (rest and fatigue) and `vehicle` (the vehicle a
  merc is aboard).
- `vehicles[]`: `name`, `sector`, `destination`, `betweenSectors`, `passengers`,
  `helicopter` and `airborne` — the helicopter's flight as data.
- `preBattle`: `active`, `sector`, `enemyCount`, `canEnter`, `canRetreat`, `canAuto` — the
  encounter panel, so a test can trigger an encounter and retreat from it.

`tests/e2e/world_map_steps.lua` tours them (plot, move, arrive, encounter, retreat,
helicopter board/fly/leave); `tests/e2e/world_map_actions.lua` tours the strategic actions
around them (split a squad, rest, doctor/patient, repair, train, inventory transfer).

## Definition of done

- `ja2.debug("campaign", spec)` sets day/time, money, difficulty, town ownership + loyalty +
  militia, sector garrisons/control, merc roster + assignments, per-merc inventory and
  quest/fact progress on the live game.
- Unknown names fail with a clear message.
- The staged state is save-game compatible: `campaign_state.lua` stages a non-default state,
  asserts `ja2.campaign()` and the status view model, saves, reloads, and asserts again.
- `tests/e2e/lib/campaign.lua` exposes `stage` / `at` / `assertState`.
- Unit tests cover name resolution and the garrison reconciliation
  (`CampaignScenario_unittest.cc`); the e2e covers the roster and the round trip.
- `./ja2 -unittests` green; `ctest -R e2e_campaign_state` green.

## Running

```bash
ctest -R e2e_campaign_state -V --output-on-failure         # from the build directory
python tools/ja2ctl.py run tests/e2e/campaign_state.lua --isolated
python tools/ja2ctl.py run tests/e2e/battle_campaign.lua --isolated --res 1920x1080
python tools/ja2ctl.py run tests/e2e/world_map_steps.lua --isolated --res 1920x1080
python tools/ja2ctl.py run tests/e2e/world_map_actions.lua --isolated --res 1920x1080
```

`campaign_state.lua` runs at 640x480 and in the resolution matrix. `battle_campaign.lua`
runs at 1920x1080 like the other `battle_*` scenarios: the native tactical HUD needs
1280x720. The two world-map tests run at 640x480 too, where the legacy map screen is up and
only the staged state is checked; the resolution sweep covers their native path.

## Roadmap

| Slice |
| --- |
| state-authoring + assertion foundation |
| world-map view steps (path plotting, squad movement, travel, encounters, the helicopter) and the strategic actions (squads, rest, doctor/patient, repair, train, inventory) — [#69](https://github.com/adhikasp/ja2-pecel-bakwan/issues/69) |
| the full arc: hiring, first battle, economy, end game |
