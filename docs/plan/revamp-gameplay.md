# Revamp gameplay: design principles

> Status: **active.** This is the *why*. The *what* and *how* live in the
> milestones and issues linked below; the track goal is
> [#203](https://github.com/adhikasp/ja2-stracciatella/issues/203). Board Track:
> `Revamp gameplay`.

## Why

Classic JA2 is a one-way ratchet: clear a town, hold it, collect mine income, buy
gear, clear the next one. Nothing pushes back, so the living-world work (the
Queen's mind, reputation, named NPCs, militia, economy) would only decorate a loop
the player already solved. Mercs are heroes: a fight has no clock, no lasting
cost, and every enemy is an interchangeable body, so "kill everything" is always
the answer.

This track makes the other ways to win (ambush, infiltration, disguise, working
with townsfolk, taking a town without a frontal assault) better than the default,
and genuinely easier in the right conditions, without forbidding the classic one.

## The thesis

**Territory is a liability that pays rent; a raiding band is an asset that builds
nothing.** Governing earns steady cash, militia and facilities but costs upkeep,
exposure and merc time. Raiding earns loot, intel, legend and delay on the Queen
but has no safe base and no income. Neither dominates. The skilled play is a
hybrid: hold **one required HQ** and raid out of it. A pure raider still needs
that one base for inventory and management.

## Rules for every design in this track

These come from looking at the game as a player, not as a system designer.

1. **One readout per town, three verbs.** The player holds one number ("how hard
   is it") and three verbs in their head: **Scout**, **Soften**, **Take**.
2. **Leads, not menus.** Every lever reaches the player as a plain lead ("the
   convoy arrives Thursday", "the foreman can get you inside"), each a small
   mission.
3. **Complexity under the hood.** Belief stores, profilers and reprisal clocks stay
   native and invisible; the player meets their effects, not their rules.
4. **The "if you..." test.** A mechanic ships only if it can be said in one
   sentence starting with "if you...". If it cannot, make it an event instead of a
   system.
5. **No new meters, few new screens.** A contact is alive or taken. Upkeep is one
   line in the existing finance screen. Overlays go on the tactical view.
6. **Testable by construction.** Every milestone ends in a deterministic e2e arc.
   If an arc cannot pass headless, the design is not finished.

## Pillars

- **A town has a front door and back doors.** Garrison, equipment, readiness and
  reinforcement reach on one side; key townsfolk, supply and comms on the other.
  How you take a town sets what you get: a frontal assault leaves a damaged town
  and a full alarm; a quiet handover leaves it intact and the Queen late to learn.
- **Frontal assault is gated, not forbidden.** Early it is simply the expensive
  route (classic onboarding is kept). In the mid and late game a fortified town
  needs a named capability, and soft levers lower the gate, so there is always a
  path.
- **Holding has a price.** Net income, not gross; the squad is the fire brigade;
  visibility draws the Queen's attention; leaving a liberated town costs
  reliability; a lost town degrades into a resistance cell, not a silent flip.
  Quiet raiding keeps her occupied with court life.
- **Mercs stop being heroes through three changes:** a clock (the radio), a force
  instead of a wave (roles, decapitation, bounds and flanking), and costs that
  stick. Kills are not the reward; objectives are.
- **Defeat is forgiving and is content.** Death never happens unless a player
  coup de grace. HP 0 is downed; a lost fight becomes capture and a rescue
  mission. The stakes are the squad's position and information, not a merc's life.
  Lethality depends on initiative, not on lowering damage across the board.
- **Stealth is forgiving, smart and observable.** An alarm is a visible message
  with a carrier, not a global flag; enemies search from belief, not truth; the
  player learns routes by observing and sees exposure before committing; outcomes
  are deterministic per action so reloading does not reroll.
- **Disguise works from visible signs.** Enemies shoot at hostile signs (a visible
  rifle), not at strangers; an unknown person in plain clothes at night is assumed
  to be a local.
- **Contacts are an exposed asset.** One or two per town from archetype templates
  plus a few hero NPCs; their fate is a story event, never a meter. No name
  without a mechanic.

## Owner decisions

- Death only by a player coup de grace; the enemy never does one and the Queen
  never executes mercs.
- One required HQ; a pure raider is designed around it.
- The Queen wakes when the player is loud, plus a slow escalation phase clock.
- A defeat costs information to the Queen first, then gear, then time.
- The late frontal assault is gated on a named capability, lowerable by soft
  levers.
- Downed enemies are revivable by their medic until secured.
- Left-behind downed mercs are captured individually.
- Stealth alarms escalate gradually and are carried by a visible messenger.
- Enemy cones and routes show only after the player has observed them.
- Stealth outcomes are seeded per action (battle and action index).

## Milestones

Build order: A, B, S, C, D. Each carries its own definition of done, cuts and
open questions; this doc does not repeat them.

| | Milestone | One line |
|---|---|---|
| A | [Downed, not dead](https://github.com/adhikasp/ja2-stracciatella/milestone/19) | Forgiving defeat: downed, wounds, initiative hit chance, capture and rescue |
| B | [Raids & ambushes](https://github.com/adhikasp/ja2-stracciatella/milestone/20) | Radio clock, enemy roles, ambush and depot raid, armor as a choice |
| S | [Stealth & disguise](https://github.com/adhikasp/ja2-stracciatella/milestone/21) | Awareness, carried alarms, recon, previews, deterministic outcomes, disguise |
| C | [Taking a town](https://github.com/adhikasp/ja2-stracciatella/milestone/22) | The readout, leads, contacts, fence, aftermath; one town three ways |
| D | [Holding vs raiding](https://github.com/adhikasp/ja2-stracciatella/milestone/23) | Upkeep, visibility, fire brigade, abandonment, graceful loss, HQ |

## Related

- Queen's Mind: [#179](https://github.com/adhikasp/ja2-stracciatella/issues/179)
  supplies her attention, belief store and levers that this track reads.
- Living world: the deed ledger ([#113](https://github.com/adhikasp/ja2-stracciatella/issues/113)),
  named NPCs ([#119](https://github.com/adhikasp/ja2-stracciatella/issues/119)),
  militia and facilities ([#120](https://github.com/adhikasp/ja2-stracciatella/issues/120),
  [#121](https://github.com/adhikasp/ja2-stracciatella/issues/121)).
- Tactical depth: stealth, suppression and NCTH
  ([#104](https://github.com/adhikasp/ja2-stracciatella/issues/104),
  [#103](https://github.com/adhikasp/ja2-stracciatella/issues/103),
  [#102](https://github.com/adhikasp/ja2-stracciatella/issues/102)).
- The existing issues rescoped by this track carry an update section or a scope
  note pointing back to #203.
