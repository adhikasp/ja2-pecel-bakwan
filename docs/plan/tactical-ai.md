# Tactical AI — behaviour baseline (functional spec)

> Status: **active.** What the campaign tactical AI actually does today, audited from the
> code ([#56](https://github.com/adhikasp/ja2-pecel-bakwan/issues/56)). It is the
> **behaviour baseline** the battle corpus asserts
> ([e2e-tactical-battles.md](e2e-tactical-battles.md), [#65](https://github.com/adhikasp/ja2-pecel-bakwan/issues/65),
> [ai-evaluation.md](ai-evaluation.md), [#59](https://github.com/adhikasp/ja2-pecel-bakwan/issues/59))
> and the contract every change to the AI is measured against. Board Track: `Modernize AI`.
> Milestone: Testable Gameplay. Audited at commit `30a05739e`.
>
> This describes the code as it stands, not the behaviour we want. It is a baseline, not a
> vanilla parity contract: the AI is being revamped on purpose, so when a change to it is
> deliberate this document changes **with** the code and the corpus is re-baselined. A
> change that moves a behaviour in here without editing this file and saying why in the PR
> is a regression. Source of truth is always the code; the `file:line` references are the
> audit's snapshot and will rot.

Scope: human/army/militia/civilian tactical AI in `src/game/TacticalAI/`, plus the
turn/AP/sight/interrupt machinery it depends on in `src/game/Tactical/`. Creature AI
(`CreatureDecideAction.cc`) is a separate hard-coded decision tree and is noted only where
it forks.

## 0. Vocabulary and the model

| Term | Meaning |
|---|---|
| **Team** | `OUR_TEAM=0, ENEMY_TEAM=1, CREATURE_TEAM=2, MILITIA_TEAM=3, CIV_TEAM=4`; teams act in ascending index order. Sides (`bSide`) group teams that fight together (militia share the player's side). |
| **Alert status** | `bAlertStatus`: `STATUS_GREEN=0, STATUS_YELLOW=1, STATUS_RED=2, STATUS_BLACK=3`. The AI's belief about the enemy, recomputed from its own knowledge. |
| **AI morale** | `bAIMorale`: `MORALE_HOPELESS=0, WORRIED=1, NORMAL=2, CONFIDENT=3, FEARLESS=4`. The AI's verdict on the fight, from `CalcMorale`. Distinct from a player merc's `bMorale` (0–100). |
| **Opplist** | Per-soldier `bOppList[other]` and team-wide `gbPublicOpplist[team][other]`, each in `[-4..5]`: `-4` heard 3 turns ago … `-1` heard this turn, `0` unknown, `1` seen currently, `2` seen this turn … `5` seen 3 turns ago. |
| **AP** | Action points; `bActionPoints`, reference `bInitialActionPoints`, carried over at most `MAX_AP_CARRIED = 5`. |
| **Turn-based (TB) / realtime (RT)** | `gfTurnBasedAI` = combat is on. TB processes one soldier of the current team at a time; RT round-robins soldiers on delays. |

The AI's decision problem each time it acts is: *from what I know, at this alert status,
with these APs, do the most valuable action I can afford.* Everything below is how that is
answered.

## 1. Where it lives and when it runs

All files are `src/game/TacticalAI/` unless noted.

| File | Responsibility |
|---|---|
| `AIMain.cc` | Entry point, action execution, alert status, AI list start/stop |
| `DecideAction.cc` | The per-alert-status option lists (`DecideActionGreen/Yellow/Red/Black`) |
| `Attacks.cc` | `CalcBestShot`, `CalcBestStab`, `CalcBestThrow`, damage estimates, burst |
| `AIUtils.cc` | `CalcMorale`, `CalcManThreatValue`, stances, movement mode, reachability |
| `FindLocations.cc` | `FindBestNearbyCover`, flee/dark/ungassed spots, item search |
| `Movement.cc` | Path legality, `GoAsFarAsPossibleTowards`, patrol, scent |
| `Knowledge.cc` | Noise and radio ("call everyone here") |
| `PanicButtons.cc` | Panic AI and the "chosen one" |
| `AIList.cc` | Turn roster: who acts, in what order |
| `BattleReport.cc` | The evaluation recorder (morale sampling, per-side tallies) |

### 1.1 Entry point

`HandleSoldierAI` (`AIMain.cc:86`) is the single per-soldier entry, called from
`ExecuteOverhead` for each soldier each overhead tick. It early-returns for soldiers
mid-animation, player-controlled without AI control, dead/dying, collapsed, asleep without
a schedule, out of sector, and so on; then dispatches to `TurnBasedHandleNPCAI` or
`RTHandleAI` (`AIMain.cc:383-405`).

- **Turn-based:** only the current team's soldiers with `SOLDIER_UNDERAICONTROL` are
  processed. A soldier already `bMoved` ends his turn.
- **Realtime:** a soldier is processed once his per-soldier `AICounter` elapses
  (`AI_DELAY = 100 ms`), unless `AI_HANDLE_EVERY_FRAME` is set. Globally, `ExecuteOverhead`
  round-robins one AI soldier every `RT_DELAY_BETWEEN_AI_HANDLING = 50 ms` for a
  `RT_AI_TIMESLICE = 10 ms` slice. `PauseAITemporarily` suspends all AI for
  `PAUSE_ALL_AI_DELAY = 1500 ms`.

### 1.2 The turn roster — the AI list

`BeginTeamTurn(team)` calls `BuildAIListForTeam(team)` for an AI team, then
`RemoveFirstAIListEntry()` → `StartNPCAI(`*s`)`. Building the list
(`AIList.cc:192-218`) keeps every member passing `SatisfiesAIListConditions` and sorts by

```
priority = bAlertStatus;  if (bVisible == TRUE) priority += 3;
```

descending, ties in slot order. So the most alert and currently-visible soldiers act
first. `SatisfiesAIListConditions` re-runs `DecideAlertStatus` on every evaluation, so a
soldier whose belief (or life) changed mid-turn is re-ranked or dropped. `StartNPCAI`
gives a soldier control, refreshes his alert status, stamps
`uiTimeSinceMercAIStart`; `EndAIGuysTurn` gives control to the next entry or calls
`EndAITurn`.

A deadlock watchdog (`AIMain.cc:368-380`) forces `EndAIDeadlock` when a turn has lasted
more than `DEADLOCK_DELAY = 15000 ms`.

### 1.3 Action points

At the start of a soldier's own turn `CalcNewActionPoints` adds `CalcActionPoints` to at
most `MAX_AP_CARRIED = 5` carried points. The base is

```
5 + ((10*Exp + 3*Agility + 2*LifeMax + 2*Dex) + 20) / 40
```

reduced by injury, breath, weight, drugs and body type. `IsActionAffordable`
(`AIUtils.cc:438`) sets a minimum cost per action (`MinPtsToMove` for movement,
`MinAPsToAttack(ADDTURNCOST)` for attacks, `AP_CROUCH` for a stance change, `AP_RADIO` to
radio, …) and returns `min <= bActionPoints`. **Turn-based** checks this before acting;
**realtime does not** — `RTHandleAI` calls `NPCDoesAct` + `ExecuteAction` directly.

## 2. Perception and knowledge

The AI never reads the world directly; it reads its own and its team's knowledge. This is
the single most important section for "is a change fair?".

### 2.1 Sight

`ManLooksForMan` (`Tactical/OppList.cc:1335`) decides whether B sees A:

- **Range** is `DistanceVisible` (`OppList.cc:881`): 360° (`MaxDistanceVisible() =
  STRAIGHT*2 = 26` tiles) for an *already known* opponent, a tank, or a muzzle flash;
  otherwise a facing cone from `gbLookDistance[facing][subjectDir]`
  (straight 13 / angle 11 / side 7 / behind 0, doubled). An angle is upgraded to straight
  for the player or a soldier at `>= RED`.
- **Light** scales range by `gbLightSighting[0][LightTrueLevel]`, the 16-level percent
  table (day index 3 = 100%, night index 12 = 43%), ×70% in rain/thunder, plus
  sunglasses/NVG (`+2`)/UV (`+4`)/NIGHTOPS (`+1`/trait) bonuses at night.
- **Cover/line of sight** is a final 3-D LOS test; smoke with a visibility cap can clamp
  range to 2.
- The cone is bypassed entirely once the opponent is known personally **or** publicly
  (`ManLooksForMan` `OppList.cc:1435-1457`).

A first sighting calls `ManSeesMan`, sets `SEEN_CURRENTLY`, increments `bOppCnt`, and at
`bOppCnt > 0` raises the soldier to `STATUS_BLACK` and records a watched location.
Losing sight flips personal (and, for the last witness, public) knowledge to
`SEEN_THIS_TURN` and decrements `bOppCnt`.

### 2.2 Knowledge decay and sharing

- `DecayOppListValue` ages an entry each turn; **seen** is remembered 4 further turns,
  **heard** 3. Out of range snaps to `0`.
- `RadioSightings` copies a soldier's personal knowledge into
  `gbPublicOpplist[team]` (whole team, no distance limit). It is gated: `AP_RADIO = 5`,
  more than one man in sector, and for the enemy team only at difficulty `>= MEDIUM`, when
  the team is already `bAwareOfOpposition`, and not for administrators.
- The team-wide copy is what makes AI teams look coordinated: any member acts on a teammate's
  sighting as if he had seen it. `gubKnowledgeValue` picks the more recent of personal vs
  public knowledge for every consumer.

### 2.3 Noise and radio

- `MakeNoise`/`ProcessNoise` (`OppList.cc:3380`) computes an effective volume per listener
  (`DecideHearing` for level/traits/night, −distance, running/sneaking modifiers) and, when
  audible, sets `HEARD_THIS_TURN` at the source plus the "most important noise"
  (`sNoiseGridno`, `ubNoiseVolume`). Gunfire at volume `>= 3` jumps the soldier to
  `STATUS_RED`.
- `MostImportantNoiseHeard` (`Knowledge.cc:125`) scores the four knowledge channels
  (personal/public hearing of an opponent, personal/public misc noise) as a negative
  `value × distance` — the *least negative* wins — and returns a reachable gridno (with
  any climb location folded in).
- `CallAvailableEnemiesTo` (`Knowledge.cc:17`) is the sector-wide "everyone investigate
  this tile" call: it sets the team's public noise gridno and volume to maximum and
  `SetNewSituation` for every member (plus gas masks). Explosions, keys and sector entry
  use it.

### 2.4 Cheats and information advantages (as implemented)

These are deliberate asymmetries to know about before "fixing" AI fairness:

- Team public knowledge and radio have no per-tile distance limit.
- The panic "chosen one" is granted keys to every door (`PanicButtons.cc:172`).
- The enemy builds `AIExposedTileMap` from the **player team's** visible-and-lit tiles each
  enemy turn and routes to avoid them (policy `avoid_light_tiles_at_night`).
- Corpse-trap warnings are placed for the AI to avoid (`GetNearestRottingCorpseAIWarning`).
- `RangeChangeDesire` reads `bConsNumTurnsWeHaventSeenButEnemyDoes` — the AI knows how long
  the player has not seen it, and expands engagement range accordingly.
- Throwing at a remembered (`SEEN_LAST_TURN`/`HEARD_LAST_TURN`) position and the
  `PreRandom(3)` screen are explicitly commented as cheats in `Attacks.cc`.
- Pathfinding is fully map-aware; only hostile positions are behind opplists.

## 3. Alert status

`DecideAlertStatus` (`DecideAction.cc:3715`) maps knowledge to status:

| From | To | Trigger |
|---|---|---|
| any | `BLACK` | `bOppCnt > 0` — an opponent is currently seen |
| `BLACK` | `RED` | no opponent in sight |
| `RED` | `RED` | never falls below RED once reached |
| `YELLOW` | `RED` | team `bAwareOfOpposition` **or** `bUnderFire` |
| `YELLOW` | `GREEN` | no important noise and no action in progress |
| `GREEN` | `YELLOW` | an important noise is remembered (own, or a friend's sighting via `ManChecksOnFriends`) |
| `GREEN` | `RED` | `bAwareOfOpposition` or `bUnderFire` |

A status change below RED calls `SetNewSituation` (force a fresh decision). Crossing into
`>= RED` also calls `CheckForChangingOrders` (widen orders: `SEEKENEMY` for militia,
`MakeClosestEnemyChosenOne` for the Warden). `ManChecksOnFriends`
(`AIMain.cc:1937`) is how a soldier learns from a visible friend: a friend `>= RED`, under
fire or dying promotes him to RED; a YELLOW friend gives him a 3-turn fake noise at the
friend's tile. `HandleInitialRedAlert` sets the team's `bAwareOfOpposition`, the single
flag that means "the army knows".

## 4. The decision pipeline

`DecideAction` (`DecideAction.cc:3635`) clears `AI_CAUTIOUS` and dispatches on
`bAlertStatus`. Every option list is "first chance that returns wins", in code order.
Green/Yellow never select a fire target; only Red (long-range/indirect) and Black
(contact) do.

### 4.1 GREEN — no suspicion

Options in order: boxes/tanks; civilian idling/schedule; point or random patrol (breath
>= 75); leave water/gas; rest if breath < 75; random patrol (`iChance = 25 + bBypassToGreen`
modified by orders/attitude/injury/fatigue); seek a friend; **look around** — turn to a new
direction, 50% biased to `bDominantDir`; else nothing.

### 4.2 YELLOW — something heard

Options in order: civilian handling; no important noise → nothing; **turn toward the
noise** (chance 60 for stationary/on-guard, else 35, +15 defensive); **radio a yellow
alert** (`iChance = 5 * WhatIKnowThatPublicDont + gbDiff[DIFF_RADIO_RED_ALERT]/2`, modified
by orders/attitude, AP `= 5`); rest; seek the noise (chance `95 + noiseValue/3`); seek a
friend in trouble (`50 - spacesAway`); **take cover** (TB only) — `FindBestNearbyCover`,
chance 25 modified; then 50% chance to fall back to GREEN; else crouch.

### 4.3 RED — enemy believed present, not seen

Options in order: no AP → nothing; **panic AI** if a panic trigger is live; gas mask /
leave gas; civilian cower; **leave a lit tile at night** if a known opponent is within
sight range; long-range throw / call spotters; rest; **HOPELESS → run away**; **radio a
red alert**; the **seek/help/hide/watch scoring loop** (below); under-fire reaction
(crouch, maybe prone, or run if shocked); face the closest known opponent; tanks turn;
**search for items**; fall back to GREEN; crouch; face the most important noise and go
prone.

The scoring loop (`DecideAction.cc:1903-2191`) only runs when `bActionPoints > MAX_AP_CARRIED`
(5) and the soldier is not a civilian. Each of SEEK, HELP, WATCH and HIDE starts at a base
score, modified by morale, orders and attitude; the highest wins; a failed option is
marked impossible and the loop retries. This is the AI's "which is worth my turn" step.

### 4.4 BLACK — enemy in sight

Options in order: no AP → nothing; panic AI; boxing; gas/breath collapse → run or forced
HOPELESS; leave water/gas; **offer surrender** (see §9.4); **attack eligibility**
(`CanNPCAttack`, reload/seek a weapon if unable); **attack evaluation** — best shot, best
throw, best stab, HTH punch, chosen by `iAttackValue`; burst decision; **take cover vs
attack**; execute the attack; **take cover**; **run away** if HOPELESS or unable to attack;
spotter radio; change stance; face the closest seen opponent; militia radio; else nothing.
Healing/medic-seeking is explicitly not implemented (`DecideAction.cc:2363-2375`).

`ExecuteAction` (`AIMain.cc:1305`) turns the chosen `AI_ACTION_*` into engine calls: it
resets `SkipCoverCheck`, then maps movement to `LegalNPCDestination`/`NewDest`, attacks to
`HandleItem`, alerts to `RadioSightings`, stance to `ChangeSoldierStance`, and so on. The
action's lifetime is owned by `ActionInProgress`/`ActionDone`.

## 5. Threat evaluation

`CalcManThreatValue(enemy, myGrid, reduceForCover, me)` (`AIUtils.cc:1828`) is the AI's
scalar "how dangerous is he", returning `-999` for an invalid/non-combatant enemy:

For a human:
```
bExpLevel + CalcActionPoints(enemy) + enemy.bActionPoints/2 + enemy.bLife/10
  + (enemy.bAssignment < ON_DUTY ? ArmourPercent/4 + marksmanship/5 + weapon.deadliness : 0)
  - bleeding/5 - (100 - breath)/10 - shock
```
then `+10%` if his last target is my tile, else `+5%` if he faces me; and, when
`reduceForCover`, `× chance-his-shot-gets-through-my-cover / 100`; an unconscious enemy is
divided by `4 + (OKLIFE - life)`. Floored at 1. Note the comment says "twice the man's
level" but the code adds it once.

`Threat[]` and `ThreatPercent[10]` (`AIMain.cc:867-872`) are the plumbing around it:

- `ThreatPercent = {20,40,60,80,25,100,90,75,60,45}`, indexed by opplist value
  (`value + 4`): `SEEN_CURRENTLY` = 100, `HEARD_3_TURNS_AGO` = 20, unknown = 25. It is both
  a confidence weight in `CalcMorale` and the `iCertainty` of a cover threat.
- `Threat[]` is a single global scratch array, filled inside `FindBestNearbyCover` from
  every opponent known personally or publicly within `MAX_THREAT_RANGE + 10*searchRange`,
  and consumed only by `CalcCoverValue`. It is not per-soldier and never cleared.

## 6. Target selection and firing

`CalcBestShot` (`Attacks.cc:91`) is the weapon target picker, called from Black:

1. Loop every merc; skip same-side/neutral; **require `bOppList == SEEN_CURRENTLY`** — a
   known-but-unseen opponent may not be shot directly.
2. `MinAPsToAttack(grid, ADDTURNCOST)` must fit `bActionPoints`;
   `AISoldierToSoldierChanceToGetThrough` must be non-zero.
3. For `ubAimTime` from `AP_MIN_AIM_ATTACK (0)` to `min(AP_MAX_AIM_ATTACK = 4, AP - minAP)`,
   maximise `iHitRate = (bActionPoints * chanceToHit) / (rawAP + aimTime)`. The AI evaluates
   shots as if standing (`AICalcChanceToHitGun`).
4. `ubChanceToReallyHit = chanceToHit * chanceToGetThrough / 100`.
5. `iThreatValue = CalcManThreatValue(opponent, myGrid, /*reduceForCover=*/TRUE, me)`.
6. `iAttackValue = (estDamage * iHitRate * ubChanceToReallyHit * iThreatValue) / 1000`.

Target stickiness: against the best so far, a candidate more than `PERCENT_TO_IGNORE_THREAT
= 50%` worse to hit is rejected outright (if the incumbent is conscious); within ±50% the
higher `iAttackValue` wins; otherwise the better chance to hit wins. So the AI does weigh
many opponents but hangs on a good current target. `EstimateShotDamage` (`Attacks.cc:1266`)
is `impact × (100 - rangeLoss + chance/4)/100`, minus head 15%/torso 75%/legs 5%-weighted
armour, floored at 1.

`CalcBestShot` also finds the best **stab/throwing-knife** attack via the same struct.
`CalcBestStab` covers melee. Black picks the highest `iAttackValue` across
shot/stab/throw, with ties going to the gun.

- **Burst:** if the gun is burst-capable, the target is conscious, there is more than one
  round left, the team is not the player's, and the APs fit, the AI bursts with probability
  `25/(aim+1)` plus attitude and (within 10 tiles) a `(10 - distance) × (1 + difficulty)`
  bonus; aggressive/slay-only add more. A burst then sets `bAimTime = BURSTING (5)` and may
  call `CalcSpreadBurst` to walk a 50% randomly-ordered chain of up to 5 adjacent seen
  targets (skipped for `ATTACKSLAYONLY`).
- **Advance to range:** `AdvanceToFiringRange` closes to the closest opponent while
  reserving one attack's AP if it can.
- **Loadout:** `CanNPCAttack` → `OKToAttack` rejects shooting self, deep water, no weapon,
  no ammo (`TryToReload` first) or an unloaded launcher; it then finds another usable
  weapon and rearranges it into the hand, dual-wielding a second pistol/SMG when possible.
  Weapon choice is "first usable in inventory order" (`FindAIUsableObjClass`), not an
  optimisation. `LoadWeaponIfNeeded` loads launcher/cannon payloads.

`OKToAttack` returns the `NOSHOOT_*` codes (`AIInternals.h:61-67`) that the decision code
branches on.

## 7. Cover, facing, stance, movement

### 7.1 Cover

`FindBestNearbyCover` (`FindLocations.cc:500`) returns a tile at least `MIN_PERCENT_BETTER =
5%` better than the current one (`CalcPercentBetter`), or `NOWHERE`.

- Search range = `gbDiff[DIFF_MAX_COVER_RANGE][difficulty]` = `{4,6,8,10,13}` for
  WIMPY…ELITE, clamped to `wisdom/8`, halved in realtime, and further limited to how far
  the soldier can afford to move. `<= 0` → no search.
- It builds the `Threat[]` list, then scores the current tile and every reachable tile in
  the rectangle using `CalcCoverValue` (`FindLocations.cc:243`).
- `CalcCoverValue` compares **my** chance to get through at the candidate tile against
  **his** chance to get through at mine, weighted by each side's threat, APs and certainty:
  `iMyPosValue = myCTGT × myThreat × myAPs`, `iHisPosValue = hisCTGT × hisThreat × hisAPs`,
  each divided by the side's `bOppCnt` when outnumbered. `hisCTGT` is pessimistically
  blended `(2×best + actual)/3` to allow for him shifting to a better angle. A
  `(20 + tiles)%` penalty applies if I cannot afford to crouch on arrival. The result is
  faded by range and knowledge (`iReductionFactor = (MAX_THREAT_RANGE - range) × certainty /
  MAX_THREAT_RANGE`). Positive = cover favours me. Adjacent teammates count −10% per man.
- The scan is deterministic (no RNG) and gated by AP cost and roaming range.

Cover is skipped after a pure turn/stance action (`SkipCoverCheck` set for
`AI_ACTION_CHANGE_FACING` and `CHANGE_STANCE`). In Black, when both an attack and cover are
available the AI compares `iOffense = chanceToReallyHit` against
`iDefense = iCoverPercentBetter`, adjusted by morale (fearless +50% offense; hopeless +50%
defense), wisdom (`>= 80` +10 defense, `< 50` −10), orders and attitude; if defense wins,
the attack is dropped for cover.

### 7.2 Stance

`StanceChange` (`AIUtils.cc:143`) returns prone/crouch when the APs fit after the attack;
`ConsiderProne` refuses when `bAIMorale >= MORALE_NORMAL` (confident troops do not go
prone) or the target is within 10 tiles. `ShootingStanceChange` (`AIUtils.cc:171`) compares
the expected chance of damage standing/crouching/prone (with a defensive bonus of
`3%/tile` past point-blank, or +10 crouched beyond a threshold) and only switches if the
improvement per stance step exceeds `20 - 3×aimTime`. Prone is hated inside `MIN_PRONE_RANGE
= 50` (5 tiles). Stance AP: `AP_CROUCH = AP_PRONE = 2`; standing→prone costs 4.

### 7.3 Facing

Facing is `bDirection`/`bDesiredDirection`, 0–7 (0 = north). The AI is *not* obliged to be
facing a new target before shooting: `MinAPsToAttack(..., ADDTURNCOST)` includes the turn,
and Black explicitly returns `AI_ACTION_CHANGE_FACING` first when a stance change and a
direction change are both wanted. Turn costs are `AP_LOOK_STANDING = 1`,
`AP_LOOK_CROUCHED = AP_LOOK_PRONE = 2` (`GetAPsToLook`), except turning while prone costs 6
(stand up to crouch first). Facing matters for spotting an unknown opponent (§2.1), and
`CalcManThreatValue` gives +5% to an opponent already facing me.

### 7.4 Movement

`LegalNPCDestination` (`Movement.cc:24`) accepts a tile only if it is on-grid, at the same
height, passes `NewOKDestination` (people, doors, roofs, blocked tiles), is not gas, not
my blacklist, and (when required) water is allowed and a path exists. `MovementMode`
(`AIUtils.cc:44-74`), chosen by action and `Urgency[alert][morale]`, decides walk vs run
(cautious soldiers walk; `AI_CAUTIOUS` also forces it). `InternalGoAsFarAsPossibleTowards`
(`Movement.cc:281`) reserves APs (default `MAX_AP_CARRIED`), respects roaming range and
rooms, stops short for `FLAG_STOPSHORT` within `STOPSHORTDIST = 5`, and stores the path.
`NewDest` may make enemy soldiers "swat" (move fast) with probability
`1/(5 - difficulty)` when a noise is close.

## 8. Grenades and thrown projectiles

`CheckIfTossPossible` (`Attacks.cc:1599`) is called after `CanNPCAttack` and only in TB.
It selects the throwable item in this order: a tank's `TANK_CANNON`; the first usable
`IC_LAUNCHER`; otherwise a `ROCKET_LAUNCHER` (50% swapped for a hand grenade when morale
`> MORALE_WORRIED`) or the first throwable grenade. If the item fits, `CalcBestThrow`
(`Attacks.cc:357`) scores it:

- **Candidate tiles are the 3×3 block** around each known opponent (`MAX_TOSS_SEARCH_DIST
  = 1`), within the weapon's toss range. Smoke/tear/mustard tiles already saturated with
  their own effect are skipped.
- **Friendly fire:** a tile is rejected if any friend is within the explosive's computed
  `safetyMargin` on the same level. (The constant `NPC_TOSS_SAFETY_MARGIN = 4` is defined
  but unused.)
- **Damage/utility:** expected damage is summed over opponents within 3 tiles, discounted
  `20%` per tile of miss distance; smoke is deliberately valued at a flat 5; a lone
  opponent is not worth a frag at low difficulty, and mortar/rocket need `>= 2` opponents
  in range or a hurt one.
- **Trajectory:** explosive guns use direct line-of-fire; thrown items use the arc
  (`CalculateLaunchItemChanceToGetThrough`); a scatter that still lands on the level gets a
  chance proportional to how much the safety margin covers the miss.
- **Accuracy:** throws always use maximum aim; the chance floor is `70%` on difficulty 0/1
  and `50%` on difficulty 2, none at 3+.
- **Rate:** there is **no per-10-turn cap** — `TOSSES_PER_10TURNS`/`SHELLS_PER_10TURNS` are
  dead (the former is redefined in `Points.cc` only as an AP-cost divisor). The practical
  limits are AP cost, ammo and the chance floors.

In Black the best throw is chosen only if its `iAttackValue` is *strictly* higher than the
best shot/stab (ties go to the gun), and it can still be cancelled by the cover-vs-attack
step. In Red, a possible toss wins outright (no comparison) and is how enemies use mortars
and rocket launchers on radioed positions. Execution is `AI_ACTION_TOSS_PROJECTILE` →
`HandleItem` → `UseThrown`/`UseLauncher`, with the throw's hit roll `PreRandom(100)` and
`RandomGridFromRadius` scatter.

## 9. Morale, panic, breaks, surrender

### 9.1 `CalcMorale` — the verdict

`CalcMorale` (`AIUtils.cc:1606`) returns `bAIMorale`. An enemy with no AI-usable weapon is
`HOPELESS` immediately.

```
for each known opponent:
    iTheirTotalThreat += pct(opponent knowledge) × CalcManThreatValue(opponent, me)
    for each living, same-side friend (or same civilian group):
        iOurTotalThreat += iTheirTerm × pct(friend's knowledge) × CalcManThreatValue(friend, opponent)
```

With `S = iOurTotalThreat`, `T = iTheirTotalThreat`, the raw score is `100 × S / (T×T)` —
the code divides `S` by `T` in place and then divides the result by `T` again. Bands:

| `sMorale` | Category | Comment |
|---|---|---|
| `<= 25` | `HOPELESS` | odds 1:4 or worse |
| `<= 50` | `WORRIED` | 1:4 … 1:2 |
| `<= 150` | `NORMAL` | 1:2 … 3:2 |
| `<= 300` | `CONFIDENT` | 3:2 … 3:1 |
| `> 300` | `FEARLESS` | better than 3:1 |

Then the category is nudged: attitude (`DEFENSIVE -1`, `BRAVE* +2`, `AGGRESSIVE +1`),
**administrators `+2`**, breath `>75` +1 / `<40` −1 / `<10` −1, life `>75` +1 / `<40` −1 /
`<20` −1, not-under-fire +1; clamped, and a `BRAVE*` soldier who lands `HOPELESS` is raised
to `WORRIED`. Morale is recomputed in Yellow only inside one branch and in Red/Black on
entry. `GetMoraleModifier` feeds it into AI marksmanship as −15/−7/0/+2/+5.

### 9.2 What morale changes

`HOPELESS` disables seek/help/watch and forces hide; multiplies the red-alert radio chance
by 3; tilts attack-vs-cover to +50% defense; and with the other retreat triggers causes
`RUN_AWAY`. Morale also sets movement urgency, whether the soldier goes prone, the cover
range term and the offensive/defensive weighting.

### 9.3 Breaks and the "chosen one"

- A **break** is the AI's own verdict: `CalcMorale(s) == MORALE_HOPELESS`. The battle report
  samples it for every soldier at the start of each player turn and counts each soldier
  once (`BattleReport.cc:270-287`); the first break is the battle's "time to disengage".
- **Panic**: a sector can hold panic bombs and triggers. `MakeClosestEnemyChosenOne` /
  `PossiblyMakeThisEnemyChosenOne` pick one soldier (unengaged, on the ground floor, with a
  reachable trigger whose tolerance `<= PercentEnemiesKilled`), make him `RED` immediately,
  and give him door keys; `PanicAI` then walks him to the trigger to pull it (or to a
  remote bomb to use the detonator). `HeadForTheStairCase` is the Queen/Joe escape to the
  Meduna staircase.
- **Cower**: unarmed civilians cower; if a flee spot exists they queue `RUN_AWAY`.

### 9.4 Surrender

In Black only, the enemy offers surrender once per battle when
`ENEMY_OFFERED_SURRENDER` is unset, the speaker is visible and at `>= half` life,
no militia is active, `NumPCsInSector() < 4`, and the enemy has at least `3×` the player's
numbers, subject to the Held-in-Alma/Interrogation quest gate. Accepting captures every
player soldier in the sector (`EnemyCapturesPlayerSoldier`), fires the captured-morale
event and ends the battle. Rejecting leaves the offer spent.

## 10. Interrupts and turn order

Interrupts are a separate out-of-turn queue (`gOutOfTurnOrder`, `gubOutOfTurnPersons`).
The system is **fully deterministic — no RNG**.

### 10.1 Eligibility

`StandardInterruptConditionsMet` (`Tactical/TeamTurns.cc:926`) requires combat, no
in-progress attack, `gubSightFlags & SIGHT_INTERRUPT`, at least `MIN_APS_TO_INTERRUPT = 4`
APs, not gassed/collapsed/neutral/on-assignment, not already seeing/seen this turn in the
relevant way, and obeying the `interrupt_after_being_under_fire` policy. `multiple_interrupts`
(default and shipped config `false`) stops a soldier who declined a previous interrupt from
getting another. Seeing and hearing have different AP minimums (`AP_CROUCH` vs
`MinPtsToMove`).

### 10.2 The duel

`CalcInterruptDuelPts` (`TeamTurns.cc:1148`) scores a soldier against one opponent from:

- base **experience level** (a robot uses its controller's level −2; a soldier controlling a robot gets −2);
- `+ GetWatchedLocPoints` for the opponent's tile when watched spots count (0–4);
- `− (bOppCnt - 1)/2` for more than two opponents seen;
- `−1` if the opponent is only heard;
- `− bShock`;
- passive side in combat: `− distance/10`;
- mover bonuses for crawling/swatting at range and a `−2` for running;
- `−2` if being bandaged;
- `+1`/trait for NIGHTOPS in darkness;
- a PC at `>= ON_DUTY` is hard-set to `−10`; a previously-seen opponent is `+1`; a
  radioed-in opponent is `+1`;
- tanks halve; the value is capped just below `AUTOMATIC_INTERRUPT = 100`.

Notably, **dexterity, agility and current AP do not enter the formula**; AP only gates
eligibility. The intended `DIFF_ENEMY_INTERRUPT_MOD` difficulty term is present in the
table but commented out.

`InterruptDuel` (`TeamTurns.cc:1307`): unilateral sight (I see him, he doesn't see me) is
an automatic win; otherwise the strictly higher `bInterruptDuelPts` wins. Ties go to the
soldier with the turn.

### 10.3 Resolution

`ResolveInterruptsVs` (`TeamTurns.cc:1534`) runs eligible opponents, builds the interrupt
list by winning margin, and hands control over. `StartInterrupt` gives the player team all
interrupters at once (amber banner) or the AI the lowest-`ubID` interrupter, hidden from
the player (`gfHiddenInterrupt`) until `NPCDoesAct` shows the message. `EndInterrupt`
restores each soldier's prior `bMoved`, re-selects the interrupted merc, and resumes. The
headless harness deliberately enters combat with `AllTeamsLookForAll(FALSE)` (interrupts
not resolved) so a scenario has a clean first turn.

## 11. Difficulty

`gbDiff[parameter][0..4]` (`AIMain.cc:53-62`), columns WIMPY…ELITE:

| Parameter | Values | Effect |
|---|---|---|
| `DIFF_ENEMY_EQUIP_MOD` | −20…20 | **dead** — table only |
| `DIFF_ENEMY_TO_HIT_MOD` | −10…10 | AI gun to-hit penalty-only (`min(0,.)`); knives add full |
| `DIFF_ENEMY_INTERRUPT_MOD` | −2…2 | **dead** — commented out in the duel |
| `DIFF_RADIO_RED_ALERT` | 50…95 | initial red-alert radio chance; half in yellow |
| `DIFF_MAX_COVER_RANGE` | 4…13 | cover/item search radius |

`SoldierDifficultyLevel` derives 0–4 from `CalcDifficultyModifier(class)` plus the class
(administrator −1, army 0, elite +1; green militia 2, regular 3). Difficulty otherwise
enters through soldier generation — stats, level, equipment and therefore APs and to-hit
skill. Higher difficulty therefore mostly means *better soldiers*, not smarter rules.

## 12. Determinism and how the corpus sees it

- The RNG is seeded once per process (`-seed`, default 1) before init; the automation clock
  is virtual. `Random` is the true RNG, `PreRandom` a pregenerated stream used to defeat
  save-scumming. Same scenario and seed → same battle.
- `ja2.state().tactical` exposes `inCombat`, `ourTurn`, `attackBusy`, enemy/militia
  per-soldier `life`, `gridNo`, `direction`, `stance`, `morale`, `aimorale` (the `CalcMorale`
  verdict) and `ap`, plus `known`/`los`/`cover`. `ja2.battleReport()` gives the outcome,
  per-side losses/damage/shots, the times, and `breaks`.
- **Not exposed today:** interrupt state (`bInterruptDuelPts`, `gfHiddenInterrupt`,
  `gubOutOfTurnPersons`, `fInterruptOccurred`), panic/chosen-one state, and alert status.
  `grep -i interrupt tests/e2e` is empty. A corpus assertion on any of these needs the
  surface added first.
- The corpus deliberately settles sighting with `AllTeamsLookForAll(FALSE)` and stages the
  player as a non-firing observer, so a fight is two AIs plus a witness.

## 13. The behaviour contract

These are the behaviours a change to the tactical AI is measured against. "Deliberate"
means: change the code, update this document, and re-baseline the corpus in the same PR.

| # | Behaviour | Where | A change must… |
|---|---|---|---|
| 1 | A soldier fires directly only at an opponent it personally `SEEN_CURRENTLY` sees | `CalcBestShot` | keep the personal-sight gate, or say why indirect/thrown fire is different |
| 2 | Attack value is `damage × hitRate × chanceToReallyHit × threat`, with a ±50% target-stickiness band | `CalcBestShot` | state the new formula and expected effect |
| 3 | Aim time is chosen to maximise hit-rate per AP, not raw accuracy | `CalcBestShot` | keep the AP-aware aim loop or re-baseline |
| 4 | Cover is only taken when at least 5% better than the current tile, and cover search is deterministic | `FindBestNearbyCover` | update the threshold/scope and the corpus |
| 5 | Cover is skipped after a pure facing/stance action | `SkipCoverCheck` | keep or explain |
| 6 | Attack vs cover is decided by morale/wisdom/orders/attitude | `DecideActionBlack` | update the weighting table here |
| 7 | A throw must beat the best shot strictly, or be the only attack | `CalcBestShot`/`CalcBestThrow` | update the comparison rule |
| 8 | Friendly fire is avoided within the explosive's safety margin | `CalcBestThrow` | keep the safety rule |
| 9 | No per-10-turn toss/shell cap exists (the constants are dead) | `AIInternals.h` | add a real cap and document it |
| 10 | A break is `CalcMorale == HOPELESS`, sampled at each player turn, counted once per soldier | `BattleReport` | keep the metric or version it |
| 11 | Morale bands are 25/50/150/300 with the modifier list in §9.1 | `CalcMorale` | update the bands/modifiers here |
| 12 | Surrender is offered once, under the §9.4 conditions | `DecideActionBlack` | update the conditions here |
| 13 | Interrupts are deterministic; the duel is level/watched-locs/shock/range-based, not dex/agility/AP | `CalcInterruptDuelPts` | re-baseline and expose the state to the corpus |
| 14 | `DIFF_ENEMY_INTERRUPT_MOD` and `DIFF_ENEMY_EQUIP_MOD` do nothing | `gbDiff` | either wire one up and document it, or leave a note |
| 15 | Public knowledge and radio have no distance limit | `UpdatePublic` | state the new scope |
| 16 | The AI reads the player's lit tiles and corpse traps | `AIExposedTileMap` | keep as an explicit, documented asymmetry |

## 14. Known quirks in the baseline

Documented so a future change is not mistaken for a regression, and so the audit does not
imply these are intended:

- `AI_ACTION_TIMEOUT_CYCLES`, `NPC_TOSS_SAFETY_MARGIN`, `TOSSES_PER_10TURNS` and
  `SHELLS_PER_10TURNS` are defined but unused as such (`AIInternals.h`).
- `DIFF_ENEMY_EQUIP_MOD` and `DIFF_ENEMY_INTERRUPT_MOD` are dead (table only / commented).
- `CalcManThreatValue`'s comment claims "twice the level"; the code adds `bExpLevel` once.
- `Threat[]` is global scratch, only filled and read inside `FindBestNearbyCover`.
- `CalcMorale`'s score divides the friendly total by the enemy total, then divides again.
- Realtime AI does not run `IsActionAffordable` before `ExecuteAction`.
- Healing / seeking a medic is stubbed out in Red.
- `FindBestNearbyCover`'s original morale-based cover formula is commented out in
  `CalcCoverValue`.
- `DecideActionRed` contains a duplicated block of unarmed-civilian cower handling.

## 15. Related

- [e2e-tactical-battles.md](e2e-tactical-battles.md) — the battle e2e harness that asserts
  this behaviour ([#65](https://github.com/adhikasp/ja2-pecel-bakwan/issues/65)).
- [ai-evaluation.md](ai-evaluation.md) — the metrics and matrix that turn a change into
  numbers ([#59](https://github.com/adhikasp/ja2-pecel-bakwan/issues/59)).
- The track plan, `docs/plan/ai-modernization.md`
  ([#216](https://github.com/adhikasp/ja2-pecel-bakwan/issues/216), not yet written); this
  audit is its tactical baseline.
- [revamp-gameplay.md](revamp-gameplay.md) — the design principles the revamped AI serves.
