# Living world NPC & quest content

> Status: **native registry implemented** (issue #156, first slice of the
> Living world NPC milestone). The registry exists, is dumped headless and is
> invariant-tested; the legacy layers keep their runtime role until each system
> is reimplemented, then retire.

## Why

The people and quest content used to be split across four layers with no single
source of truth and no headless way to read it ([#110 research comment](https://github.com/adhikasp/ja2-pecel-bakwan/issues/110#issuecomment-5975427902)):

| Layer | Source | Covered |
|---|---|---|
| Roster | `assets/externalized/mercs-profile-info.json` | 159 profiles |
| Script records | `assets/externalized/script-records-NPCs.json` + `Npcdata.slf`/`NpcData/*.npc` | 10 JSON overrides, 130 binary records |
| Quest ids/titles | `src/game/Strategic/Quests.h`, `QuestText.cc` | 24 used quests |
| Hardcoded triggers | ~20 `StartQuest`/`EndQuest` call sites + the action switch | — |

Reading the quest state machine meant reverse-engineering all four each time.

## The native layer

`src/game/Content/PeopleContent.{h,cc}` is one compiled registry:

- `NpcDef` — id, kind, internal name, home sector(s), placement, schedule,
  opinion seed (feeds #119), and typed `QuestLink`s (`Giver`/`Resolver`/`Dialogue`).
- `QuestDef` — id, name, title, givers, resolvers, `selfResolving`, dialogue
  profiles, prerequisites, reward, reputation/deed hooks, note and stages.
- `ApplyQuestTransition` — the typed `{quest, Start|End}` transition. Dialogue
  actions route through it; no new raw `StartQuest`/`EndQuest` call sites.

The table in `PeopleContent.cc` is **generated once** by
`tools/generate_people_content.py` from the four layers (including the binary
`.npc` records) and is then edited natively, like the rest of the content. It
matches the #110 inventory exactly: 159 named characters, 24 quests.

```
python tools/generate_people_content.py                       # regenerate
python tools/generate_people_content.py --check                # fail if stale
python tools/generate_people_content.py --game-dir "<install>" # non-default data
```

The generator needs the original game data for the binary `.npc` records;
without `--game-dir` it still merges the JSON layers and the quest enum.

## Invariants

`PeopleContent_unittest.cc` asserts, in the `-unittests` run:

- every roster profile has a def (159) and every def resolves back to itself;
- every quest slot has a def (24), and its title matches `QuestDescText`;
- every `QuestLink`, giver, resolver and dialogue profile resolves;
- every quest has a giver **and** a resolver, or is explicitly `selfResolving`;
- the extracted links and aggregates match the #110 tables (MIGUEL, AUNTIE,
  CARMEN, MARIA/`script-records-NPCs.json` overrides, Skyrider, the creature
  miners, the placement sectors).

## Read it headless

```
python tools/ja2ctl.py eval 'return ja2.game.npcs()'
python tools/ja2ctl.py eval 'return ja2.game.quests()'
python tools/ja2ctl.py eval 'return ja2.game.quests()[2].status'   # live gubQuest
```

`ja2.game.quests()` carries the live `gubQuest` status, so an e2e script can
assert a transition ("talk to Auntie → `BLOODCATS` becomes `IN_PROGRESS`")
without a screenshot.

## Adding an NPC or a quest

1. Edit `src/game/Content/PeopleContent.cc` directly. Reference quests by the
   `Quests` enum (`QUEST_FOOD_ROUTE`) and people by the `ProfileID` enum token
   (`MIGUEL`); never a raw `u8`.
2. Keep the invariants: a quest needs a giver and a resolver, or
   `selfResolving = true` with a `note` saying who the hardcoded trigger is.
   Every referenced quest and profile must exist.
3. The roster and the engine's `MERCPROFILESTRUCT` still supply the runtime
   stats; the registry is the content graph (who is who, who gives what).
4. Run `-unittests`. Do **not** re-run the generator on a hand-edited table
   unless you intend to discard the edits.

## Follow-ups (candidates)

- Migrate the remaining `StartQuest`/`EndQuest` call sites to
  `ApplyQuestTransition`; retire the raw calls.
- Drive the quest state machine from the registry (givers/resolvers/stages)
  instead of the scattered fact checks.
- Feed the `opinionSeed` and `homeSectors` into [#119](https://github.com/adhikasp/ja2-pecel-bakwan/issues/119)
  (named NPCs with opinion and memory).
