# E2E campaign state — design

> **Status: first slice implemented.** The plan below is the contract for the track; the
> first tests built on it are:
>
> - `tests/e2e/campaign_state.lua` — author a non-default campaign state, assert it through
>   `ja2.campaign()` and the status view model, save, reload and assert it again.
> - `tests/e2e/battle_campaign.lua` — author a state, walk into a controlled town with
>   townsfolk, then step into a tactical battle in the loaded sector.
> - `tests/e2e/lib/campaign.lua` — the shared steps (`stage`, `at`, `assertState`,
>   `enterSector`, `enterTown`, `stepIntoBattle`).
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
```

`campaign_state.lua` runs at 640x480 and in the resolution matrix. `battle_campaign.lua`
runs at 1920x1080 like the other `battle_*` scenarios: the native tactical HUD needs
1280x720.

## Roadmap

| Slice |
| --- |
| this state-authoring + assertion foundation |
| world-map view steps (path plotting, squad movement, travel) |
| the full arc: hiring, first battle, economy, end game |
