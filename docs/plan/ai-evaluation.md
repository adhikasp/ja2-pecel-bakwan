# AI evaluation: metrics and the harness

> Status: **active.** The measurement side of the tactical battle corpus
> ([e2e-tactical-battles.md](e2e-tactical-battles.md), issue #65); this document defines
> what "the AI got better" means as numbers and the harness that reports them (issue #59).
> Board Track: `Modernize AI`.
>
> - `ja2.battleReport()` — any staged battle, read back as data.
> - `tests/e2e/battle_ai_eval.lua` — one matchup, played out AI-only, printed and asserted.
> - `python tools/dev.py ai-eval` — the matchup matrix, as a table (`tools/ai_eval.py`).

## Why

The tactical AI is being changed on purpose: equipment-aware loadouts (#108), leaf
tunables (#58), morale, cover, target selection. Every one of those claims "the AI is
better at X", and the corpus can stage the fight to check it — but "it felt smarter" is
not a review and a scenario that only asserts "the militia won" is not a measurement.
What a change did to the AI is a delta in a handful of numbers, measured on the same
matchups, at the same seed, before and after.

The rules are the repo's usual ones: the metrics are computed in native C++, from the
battle's own events, not from a screenshot; the harness is deterministic; a matchup is a
bounded run in its own process.

## The battle report

Every battle a scenario stages carries a **battle report**: the recorder in
[`BattleReport.h`](../../src/game/TacticalAI/BattleReport.h) starts it when combat starts,
listens to the battle's own events while it runs (shots, impacts, deaths), samples the
AI's morale verdicts at each player turn, and freezes it when combat ends. A lull (combat
exits with both sides still standing) freezes it; if the fight resumes in the same sector
the report continues — a lull is a pause, not a new battle, so losses and time span the
whole engagement. Scenarios read it back with `ja2.battleReport()` (the Lua shape is
below); it is session data, not save data, and the next staged battle starts a fresh one.

The report covers the battle's two sides:

- **player** — the player's soldiers: `OUR_TEAM` mercs and `MILITIA_TEAM` militia.
- **enemy** — `ENEMY_TEAM` soldiers. Other teams (civilians, creatures) are not counted.

```lua
local r = ja2.battleReport()
-- r = {
--   started = true, finished = true, sector = "E11",
--   outcome = "player", endedBy = "wiped_out",
--   rounds = 3, seconds = 41.2,
--   contactSeconds = 12.4, firstCasualtySeconds = 18.0,
--   firstBreakSeconds = 31.5, disengageSeconds = 31.5,
--   objective = { grid = 12074, side = "player", held = false },   -- when one was declared
--   player = { soldiers = 20, alive = 19, dead = 1, wounded = 7,
--              lifeStart = 1195, lifeEnd = 980,
--              shots = 55, hits = 30, impacts = 28,
--              damageDealt = 420, damageTaken = 215, breaks = 2 },
--   enemy  = { ... the same fields ... },
-- }
```

## The metrics

### Battle outcome — who held the field

`outcome` is decided when combat ends, from the living soldiers of each side:

| value | meaning |
|---|---|
| `"player"` | the enemy side has nobody left standing |
| `"enemy"` | the player side has nobody left standing |
| `"draw"` | both sides still have soldiers (a lull ended the fight), or both are gone |
| `"unresolved"` | combat is still running |

`endedBy` says how it ended: `"wiped_out"` (one side is gone), `"lull"` (combat ended
while both sides still stand — the game's "nobody has seen anybody for a while" exit), or
`"in_progress"`.

### Losses inflicted vs taken

Each side's tally carries what it lost and what it did:

| field | meaning |
|---|---|
| `soldiers` | the side's soldiers in the sector when the battle started |
| `alive` / `dead` / `wounded` | standing at the end / lost during it (counted from the death events) / alive below full life |
| `lifeStart` / `lifeEnd` | the side's total life at the start and at the end |
| `shots` / `hits` / `impacts` | trigger pulls, pulls whose roll connected, rounds that arrived on a counted soldier (either side) |
| `damageDealt` / `damageTaken` | damage the side's rounds did to the other side / took from it |
| `breaks` | soldiers the AI rated HOPELESS (its run-away verdict), first time only |

`damageDealt` and `damageTaken` conserve: what one side dealt, some side took
(`player.damageDealt + enemy.damageDealt == player.damageTaken + enemy.damageTaken`).
They are not per-direction mirror images — a side's own friendly fire lands in its
`damageDealt` without landing in the other's `damageTaken`. `shots >= hits`; `impacts`
is not bounded by `hits`, because whether a round reaches a body and whether the roll was
aimed true are two separate decisions in the pipeline (see
[equipment-revamp.md](equipment-revamp.md)).

### Time to disengage — how long the fight lasted

Times run on the engine clock (`GetJA2Clock`, the clock that advances during turn-based
combat — the world clock is paused then), and are reported in seconds since combat
started:

| field | meaning |
|---|---|
| `contactSeconds` | the first shot of the battle (`-1` if none) |
| `firstCasualtySeconds` | the first death (`-1` if none) |
| `firstBreakSeconds` | the first soldier either side rated HOPELESS (`-1` if none) |
| `disengageSeconds` | **time to disengage**: the first break, or the end of combat if nobody broke |
| `rounds` | player turns taken while in combat |
| `seconds` | total elapsed when finished; so far while unresolved |

A break is the AI's own verdict (`CalcMorale == MORALE_HOPELESS`), sampled at each player
turn — the moment a soldier decides the fight is lost. "Time to disengage" is when the
battle stopped being a battle: the first side to break, or the end if it was fought to
the last man.

### Objective taken — did the side hold what it came for

A scenario can declare one objective in the battle spec:

```lua
battle.stage{ ..., objective = { grid = 12714, side = "player" } }
```

The report answers whether that side held it when combat ended: `objective.held` is true
when a living soldier of `side` stands on `grid` at the end (a 3×3 area is not "held" — an
objective is a tile, and holding it means standing on it). No objective declared means no
`objective` field. This is the metric that separates "the AI won" from "the AI won what it
was there for": a fight can be won by casualties while the building changed hands, and
that is exactly the difference an evaluation has to show.

## The harness

### One matchup — `tests/e2e/battle_ai_eval.lua`

Stages one AI-vs-AI matchup on E11 (the battle_militia battlefield: a loose militia line
with a support echelon, an enemy assault line a few tiles inside pistol range), plays it
out by pressing nothing but End Turn, reads the report, asserts it against the field (the
counts add up, every round of damage is counted once from each end, the outcome matches
who is standing), and prints one machine-readable line:

```
AIEVAL {"verdict":"...","matchup":{...},"report":{...}}
```

The player's merc is an observer at the sector entry, over 40 tiles away: he is a real
soldier of the player's side (so the player has a turn to end), but he never fires and is
never seen, so the fight is the two AIs' alone. He is counted in the player tally (one
soldier, no shots, no damage taken), so the matrix reads `player` as the militia plus that
one observer. The objective is the enemy line's centre, to be held by the player: whether
the AI takes the ground, not just the casualties.

The `verdict` is the harness's summary of the AI force alone, because the report's `outcome`
is about the two *sides* and the observer keeps the player's side standing: `militia` (the
enemies are gone), `enemy` (the militia are gone), `mutual` (both) or `stalemate` (both
still on the field). A routed militia that leaves one survivor hiding reads `stalemate` in
the verdict and in the losses (`militia 2/9`, `enemy 10/0`).

The matchup comes from `-arg` as `key=value,key=value`:

| key | default | meaning |
|---|---|---|
| `militia` | `20` | militia on the player's side |
| `class` | `green` | `green`, `regular` or `elite` militia |
| `enemies` | `10` | enemy soldiers |
| `enemy_class` | `administrator` | `administrator`, `army` or `elite` |
| `rounds` | `12` | the round bound: a fight still running at it is reported `unresolved` |

Forces are spread over the E11 layout: N militia take evenly spaced slots of the 20-slot
line (support echelon included), M enemies evenly spaced slots of the 10-slot line, so the
same battlefield scales down to smaller fights. A fight still running at the round bound is
reported `unresolved` — the stall is itself a result (`--rounds` raises the bound), and the
report is a live snapshot of it: who is standing, the damage so far, the first break.

### The matrix — `python tools/dev.py ai-eval`

Runs the default matchups, each in its own isolated process (one battle per process is the
corpus contract), and prints them as a table:

```bash
python tools/dev.py ai-eval                 # the default matchups
python tools/dev.py ai-eval --matchup elite # one of them
python tools/dev.py ai-eval --json eval.json            # save the raw reports
python tools/dev.py ai-eval --baseline eval.json        # deltas against a previous run
python tools/dev.py ai-eval --seeds 1,2,3               # a distribution, not one sample
```

| matchup | the question |
|---|---|
| `even` | 10 green militia vs 10 administrators — the baseline fight |
| `numbers` | 20 green militia vs 10 administrators — what numbers buy |
| `army` | 10 green militia vs 10 army — what a better enemy class buys |
| `elite` | 10 green militia vs 10 elite — the top of the class ladder |

The table is the evaluation: verdict, rounds, time, both sides' losses and damage, first
break and objective. `--json` writes the reports as data; `--baseline` compares a run
against a saved one and prints the deltas (verdict changes and per-side loss/damage moves),
which is how a change to the AI is judged: same matchups, same seed, what moved.

## Determinism and what a change means

- The RNG is seeded once per process and the automation clock is virtual, so one matchup
  at one seed is one deterministic sample; `--seeds 1,2,3` runs it at several seeds for a
  distribution (a win rate, not a single result).
- Compare at the **same seed**. A different seed is a different battle, not a delta.
- A change that makes the enemy AI better shows up as *the player losing more* in the same
  matchup before the winner changes: more `enemy.damageDealt`, more `player.dead`, an
  earlier `firstBreakSeconds`. The outcome flipping is the last thing to move, not the
  first.
- The report is per battle, so the matrix is not a campaign: it says nothing about the
  strategic layer, only about the fight.

## Not in this slice

- No Monte Carlo by default: one seed is the corpus contract; `--seeds` is there for a
  distribution when a question needs one.
- No in-game summary screen: the report is the data; a screen would be a separate UI issue.
- No per-soldier breakdown in the report: per-side tallies are the metrics; the scenarios
  (`battle_militia.lua`) still assert per-soldier behavior (moves, stances, morale).
