# Equipment revamp: design principles

> Status: **active.** This is the *why*. The *what* and *how* live in the issues
> linked below. Milestone:
> [Tactical revamp signature systems](https://github.com/adhikasp/ja2-pecel-bakwan/milestone/10).
> Track goal: [#259](https://github.com/adhikasp/ja2-pecel-bakwan/issues/259). Board Track: `Revamp gameplay`. Sibling doc: `docs/plan/revamp-gameplay.md`.

## Why

Vanilla JA2 equipment is a shopping list: you buy the biggest number, and a gun is
a damage figure plus a magazine. 1.13 fixed the depth (NAS attachments, LBE, ammo
types, condition) but at the cost of a catalog of hundreds of near-duplicates and a
lot of bookkeeping. We want the depth without the chore: **few kinds of gear, each
with a visible trade-off, each a decision the player makes before the fight.**

## The thesis

**The loadout is the plan.** A merc walks into a fight with what the player chose,
and that choice (which gun, which ammo, which pockets, how much weight) decides what
the squad can do: breach, suppress, stay quiet, punch armor, or stay light enough to
run. A fight is mostly won or lost at the equipment screen and the map, not by
buying the largest gun.

## Rules for every design in this track

1. **Every item answers "what does it cost me?"** A bonus with no price is a
   bug. A suppressor costs range, a plate costs weight, AP ammo costs damage to
   unarmored targets.
2. **Curate, do not catalog.** One compiled starter set per class. A new item must
   do something no existing item does; "same but +5%" is rejected.
3. **A counter for every strength.** Ammo, armor, range and condition resolve
   through one damage pipeline, so each tier has a named answer (flank, AP,
   explosives, suppression, stealth).
4. **Bookkeeping must pay rent.** A number the player has to track (condition,
   rounds, weight) ships only if it creates a choice. Wear is slow, repair is a
   loop with a payoff, and magazines are items, not round counts.
5. **Readout before commit.** The player sees range, hit chance, penetration and
   noise before pulling the trigger (see the weapon comparison UI).
6. **Native-first, compiled, unit-tested.** Item, ammo and attachment definitions
   are C++ with schema-invariant tests. No runtime rules JSON. Leaf tunables
   (wear rate, spread, slot counts) are toggles.
7. **Testable by construction.** A loadout can be built headless and a shot's
   result asserted deterministically. If it cannot be asserted, it is not done.

## Pillars and the picks we are proposing

These are starting positions for the brainstorm in each issue; the owner decides.

### Platform and modules (NAS-lite)

- A weapon is a **platform with typed slots**: optic, muzzle, underbarrel, side
  rail, and the magazine well. Three to five slots per weapon, set by weapon class.
- Every attachment has a role and a trade-off, about 15 to 20 in the whole game.
- Compatibility is by **mount type** (rail, muzzle thread, underbarrel), never a
  per-weapon whitelist. This is the schema every other system hangs off.

### Ammunition as a decision

- About six calibers: pistol, PDW or SMG, intermediate rifle, battle rifle,
  shotgun, heavy.
- Four ammo types: **ball, AP, hollow-point, subsonic**, with explicit counters.
  HP beats the unarmored and loses to armor; AP beats armor and underperforms on
  flesh; subsonic is quiet and short-ranged.
- Reloading is **whole magazine**; magazines are inventory items; loose ammo fills
  magazines in the sector inventory, not in the middle of a fight.

### Load-bearing equipment as the belt system

- Vest, belt and pack each provide pockets of typed sizes. **What you carry is the
  loadout choice**, capped by pockets and by weight.
- Nesting depth is one (a pouch holds items; a pouch does not hold pouches).
- Weight feeds the existing encumbrance work, so a heavy loadout is slower and
  tires sooner, a real price.

### Armor in layers

- Soft vest, plate carrier with swappable plates, helmet. Plates degrade; a soft
  vest does not stop rifle rounds at all.
- Armor tiers gate small arms and create the "flank, AP, explosives or stealth"
  choice of the Raids & ambushes milestone.

### Condition that earns its keep

- Slow wear, jams that matter mostly for neglected or cheap guns, overheating only
  on sustained automatic fire (which feeds suppression).
- Cleaning kits and a mechanic's skill close the loop; no per-shot bookkeeping.

### One damage pipeline

Hit chance, penetration, damage and wear are resolved in **one deterministic
pipeline**: weapon x ammo x range x armor x condition. Ammo, armor, NCTH and
condition are not four systems; they are four inputs to this one. That is the
interdependency hub of the whole track.

### Weapon roles, not weapon counts

The roster is picked by **role**: close-quarters PDW, assault rifle, marksman rifle,
battle rifle and machine gun (suppression), shotgun (breaching), pistol (sidearm,
quiet), heavy and launcher. A weapon is in only if its role is distinct.

## Owner decisions

*(None recorded yet; the owner fills this section as each issue is decided.)*

## Dependency order

The build order follows the schema. Each issue lists what it depends on.

```text
  #96  slots + LBE model ──┬──> #97  ammo and magazines ──┐
  (the schema)             │                              ├──> pipeline ──> #102 NCTH
                           ├──> #101 armor and gear ──────┤    (#260)         #220 armor choice
                           │                              │                  #98  condition
                           └──> #100 weapons + attachments┘
  #140 encumbrance ─> LBE weight          #103 suppression <─ noise / overheating
  loadout UI (#260) <─ #96   readout #141 <─ pipeline   AI #108 <─ #101, #97
  equipment fixtures (#260) gate every one of the above
```

## Issues

Existing, now rescoped here: [#96](https://github.com/adhikasp/ja2-pecel-bakwan/issues/96)
slots and LBE, [#97](https://github.com/adhikasp/ja2-pecel-bakwan/issues/97) ammo,
[#98](https://github.com/adhikasp/ja2-pecel-bakwan/issues/98) condition,
[#100](https://github.com/adhikasp/ja2-pecel-bakwan/issues/100) weapons catalog,
[#101](https://github.com/adhikasp/ja2-pecel-bakwan/issues/101) armor and gear,
[#102](https://github.com/adhikasp/ja2-pecel-bakwan/issues/102) NCTH,
[#108](https://github.com/adhikasp/ja2-pecel-bakwan/issues/108) equipment-aware AI,
[#140](https://github.com/adhikasp/ja2-pecel-bakwan/issues/140) encumbrance,
[#141](https://github.com/adhikasp/ja2-pecel-bakwan/issues/141) readout UI,
[#220](https://github.com/adhikasp/ja2-pecel-bakwan/issues/220) armor as a choice.
New issues: [#260](https://github.com/adhikasp/ja2-pecel-bakwan/issues/260)
damage resolution pipeline,
[#261](https://github.com/adhikasp/ja2-pecel-bakwan/issues/261) weapon roles and noise,
[#262](https://github.com/adhikasp/ja2-pecel-bakwan/issues/262) native loadout screen,
[#263](https://github.com/adhikasp/ja2-pecel-bakwan/issues/263) equipment e2e fixtures.

## Out of scope

- 1.13's full item catalog, and any 1.13 code or data (ideas only, reimplemented).
- Per-round ammo tracking, per-shot jam bookkeeping, nested LBE beyond one level.
- Backward compatibility with old saves or old item ids.
