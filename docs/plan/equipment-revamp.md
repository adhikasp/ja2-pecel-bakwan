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

### The damage pipeline (#260)

Two questions were left open when the pipeline was planned, and the
implementation answers them:

- **Penetration model: tiered threshold plus residual.** A round's penetration
  power at the range is compared to the tier's threshold (both scaled by the
  armour's condition). At or over it the round gets through; the surplus over
  the threshold is the **residual**, reported rather than banked - the "through
  by N" the readout shows. Under the threshold the round is stopped, and one
  that came within half the threshold of it still bruises
  (`BluntTrauma`). No ricochet and no second roll: the only random value in a
  shot is the hit roll the caller takes from the seeded stream.
- **Armour condition enters at two points, both the defender's problem.** A
  worn plate soaks less (its protection is scaled by condition) and asks less
  (its threshold is scaled too), so shooting somebody's armour is a way to win
  a fight. A tier worn to nothing is read as the unarmoured cell, so a ruined
  vest is exactly no vest.

The counter table is one explicit cell per ammo type per armour tier
(`AmmoVersusArmour`), with a unit-tested golden table and the invariants: against
armour, AP always absorbs less and penetrates more than ball (at any armour
condition) and never does less damage with the armour intact, while never
beating ball on flesh; HP is never better than ball against a plate and is the
best round against a bare man; subsonic is quietest and shortest-ranged. More
armour is never worse for the man wearing it. The numbers live in
`src/game/Equipment/DamagePipeline.cc`; the leaf tunables (wear rate, range
falloff) are game policy toggles.

### The chance to hit (#102)

The four open questions on the aim core were settled by the owner:

- **Aim ceilings are per weapon discipline, and a marksman's is highest.** A
  weapon may spend at most its discipline's extra AP aiming (SMG 3, shotgun 2,
  rifle 5, machine gun 3, **marksman 8**); the targeting cursor and the AI respect
  it, and the targeting cursor reuses its most-aimed art past the field weapons'
  ceiling. A marksman also gets more out of each click (`AimScale`), so its
  ceiling is higher both in clicks and in payoff.
- **The aim curve is a formula with diminishing returns**, not a table: click k
  is worth `scaled * 12 / ((k+1)(k+2))`, so the first click buys the most and the
  total equals the vanilla flat bonus at four clicks. The numbers live in
  `src/game/Equipment/AimModel.cc`.
- **Automatic fire is a role gate.** Only rifles and machine guns
  (`SupportsAutofire`) may lay down continuous fire; everything else keeps the
  fixed burst its data declares. Autofire fires until the AP or the magazine runs
  out, at one AP per round.
- **Recoil decays, it does not reset.** A burst builds a pool on the shooter that
  costs accuracy immediately and bleeds off while the soldier is not firing, so it
  carries into the next exchange (an interrupt, an overwatch shot). The pool is
  the shared "rounds fired this burst" that overheating (#98) and suppression
  (#103) read.

The rules are pure and unit-tested in `src/game/Equipment/AimModel.{h,cc}`; the
adapter that reads a soldier into them is in `src/game/Tactical/Weapons.cc`, and
the lane asserts it end-to-end in `tests/e2e/battle_ncth.lua`.

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
