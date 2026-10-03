# Auto-resolve — functional spec (M1)

The strategic auto-resolve screen (`AUTORESOLVE_SCREEN`): a sector battle resolved
without tactical play. It creates temporary soldiers for every participant and steps a
combat simulation, drawing one card per merc, militia and enemy.

Source of truth for the legacy screen: `src/game/Strategic/Auto_Resolve.cc` /
`Auto_Resolve.h`. This document is the **parity contract** for the native rebuild
(Phase 7, `docs/plan/native-modern-game.md`). Nothing here may be dropped silently; a
deliberate omission is recorded with a reason.

## 1. Entry and exit

- **Entered** from the pre-battle interface (`Auto Resolve` button / automatic start),
  from a retreat that leaves militia, or from a tactical battle with no mercs left
  (`HandlePotentialBringUpAutoresolveToFinishBattle`). `EnterAutoResolveMode(sector)` sets
  `SetPendingNewScreen(AUTORESOLVE_SCREEN)`, allocates the model and flags
  `fEnteringAutoResolve`.
- **First frame** builds the roster (`CalculateAutoResolveInfo`,
  `CalculateSoldierCells`), the interface, the team leaders and attack values, and runs
  the pre-battle → auto-resolve zoom transition.
- **Exited** when `fExitAutoResolve` is set (Done): `RemoveAutoResolveInterface()` does
  all post-battle bookkeeping (deaths, captures, militia promotions/removals, enemy
  tracking, sector control, `WarpGameTime`) and the screen returns to `MAP_SCREEN`. A
  lost model (`gpAR == nullptr`) also returns to the map.
- The `ja2.debug("autoresolve")` automation hook fakes an encounter in the loaded
  sector and enters directly (used by the parity tour).

## 2. States

| State | Flag | Native UI |
|---|---|---|
| Entering | `fEnteringAutoResolve` | brief transition; native panel fades in |
| In progress | `ubBattleStatus == BATTLE_IN_PROGRESS` | speed + retreat controls live |
| Paused | `fPaused` | Pause button active |
| Play | `!fPaused` | 1× speed |
| Fast | `uiTimeSlice == 4000` | Fast button active |
| Finish | `uiTimeSlice == 0xffffffff`, `fSound == FALSE` | instant; result shows |
| Surrender offer | `fPendingSurrender` | only Accept / Refuse |
| Victory | `BATTLE_VICTORY` | result banner + Done + Bandage |
| Defeat | `BATTLE_DEFEAT` | result banner + Done |
| Retreat | `BATTLE_RETREAT` | result banner + Done |
| Surrendered | `BATTLE_SURRENDERED` | result banner + Done |
| Captured | `BATTLE_CAPTURED` | result banner + capture text + Done |

## 3. Information shown

- **Header** (`STR_AR_ENCOUNTER_HEADER` for an encounter, `STR_AR_DEFEND_HEADER` for an
  invasion or creature attack), and the **sector** name (`GetSectorIDString`, Z ignored).
- **Remaining forces**: "good" = alive mercs + militia, "bad" = alive enemies. The
  legacy colours it red / yellow / green by the thresholds
  `good*3 <= bad*2`, `good*2 >= bad*3`, otherwise yellow. The native UI shows it as a
  badge with the same thresholds.
- **One card per participant**: portrait (a merc's big face, or the side's generic face from
  `interface/smfaces.sti` — the skull frame when dead), health text (`zHealthStr`; the soldier's name when dead),
  health/breath/morale bars for mercs, and status: team leader, bleeding, hit flash, robot, EPC, retreating/retreated.
- **Elapsed battle time** once the battle is over (the total is tripled on the first
  end frame; a retreat adds 5 minutes). Format: `STR_AR_TIME_ELAPSED`, `mm h ss m`.
- **Result** (`STR_AR_OVER_*`), red for defeat/surrender/capture, green for victory,
  yellow for retreat. `STR_ENEMY_CAPTURED` is shown with the captured result.
- **Surrender offer** (`STR_ENEMY_SURRENDER_OFFER`) while pending.

## 4. Actions

| Action | Legacy | Native |
|---|---|---|
| Pause | Pause button / Space | `speed('pause')` / Space (handled by the legacy input drain) |
| Play | Play button | `speed('play')` |
| Fast | Fast button | `speed('fast')` |
| Finish | Finish button | `speed('finish')` |
| Retreat one merc | click the merc cell | `retreat(index)` |
| Retreat all | Retreat button (disabled with no mercs) | `retreat('all')` |
| Bandage | Bandage button (victory with wounded and a kit) | `bandage` |
| Accept surrender | Yes | `yes` |
| Refuse surrender | No | `no` |
| Done | Done (win/lose) | `done` |
| Leave | Alt+X | `HandleShortCutExitState` via the legacy input drain |

Keyboard: **Space** toggles pause/play, **Alt+X** quits — both run through the legacy
`HandleAutoResolveInput`, so the native screen keeps the same bindings. Native buttons
carry tooltips with the shortcut.

## 5. Edge cases (parity)

- **No militia**: mercs fight alone.
- **Zero mercs**: militia vs enemies; Retreat is disabled.
- **EPCs**: flagged, no capture allowed (`fCaptureNotPermittedDueToEPCs`); EPC attack is
  0 and defence 1000; EPC-only survivors are killed on defeat.
- **Robot**: fights as a merc; cannot retreat alone, retreats with its controller; a
  controllerless robot is disabled and can be instantly killed if it is the last one.
- **Surrender/capture**: only days ≥ 4, 2–3 conscious mercs, no militia/EPCs/robot, all
  mercs below 60% life, enemies at least 2× mercs and conscious; otherwise no offer.
- **Creatures**: spitting male / biting female creatures, no strategic-group association,
  civilians-eaten counter on defeat/retreat.
- **Counts**: 20 mercs, `MAX_ALLOWABLE_MILITIA_PER_SECTOR` militia, 32 enemies; extra
  enemies beyond 32 are slaughtered off-screen on victory.
- **Battle-end side effects** (morale, loyalty, sector control, music, history, first
  battle, `gsEnemyGainedControlOfSectorID` / `gsCiviliansEatenByMonsters`) must stay in
  the same order and conditions. In the legacy code they run from the render path; the
  native screen calls the legacy handle every frame, so they still run exactly once.

## 6. Native design (M2)

The native panel is opaque and replaces the legacy blitter panel. Layout, 1920×1080 dp
reference, built from the design-system components:

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  ENCOUNTER IN SECTOR      <sector>                          <forces badge>     │
├───────────────────────────────┬──────────────────────────────────────────────┤
│  YOUR FORCES                  │  ENEMIES                                     │
│  MERCS (n)                    │  (n)                                         │
│  [face][name][hp/en/mor] ...  │  [icon][name] ...                             │
│  MILITIA (n)                  │                                              │
│  [icon][name] ...             │                                              │
├───────────────────────────────┴──────────────────────────────────────────────┤
│  [Pause][Play][Fast][Finish]        [Retreat all]      [Bandage][Done]        │
└──────────────────────────────────────────────────────────────────────────────┘
        result banner (Victory/Defeat/...) + elapsed time, when over
        surrender offer modal (Accept / Refuse), when pending
```

- Two columns; the friendly side stacks **Mercs** then **Militia**, the enemy side lists
  enemies. Cards wrap in a flex grid; the whole body scrolls if a side overflows.
- A dead card is dimmed with the name; a retreating/retreated card shows that status.
- An `audit-skip` scrim/decoration keeps the layout audit clean.

## 7. Wiring

- Route `AUTORESOLVE_SCREEN → "autoresolve"` (`src/game/NativeUI/NativeUI.cc`), default
  `native` in `UiMode.cc`. A native screen cannot start (RmlUi missing or output below
  1280×720) → the legacy screen runs.
- `AutoResolveNative.cc` owns the screen: every frame it calls the legacy
  `AutoResolveScreenHandle()` (which advances the simulation, drains its hotkeys and
  applies the battle-end side effects), then refreshes the view model.
- `AutoResolveBridge.h` is the game-side read/command API over the file-local
  `AUTORESOLVE_STRUCT`; the native screen never touches `gpAR` directly.

## 8. Verification (M4)

- **Unit**: `AutoResolveModel_unittest.cc` (forces colour thresholds and cell
  classification) and the `ScreenKey(AUTORESOLVE_SCREEN)` assertion.
- **Parity tour** (`tests/e2e/autoresolve_parity.lua`): enter with
  `ja2.debug("autoresolve")`, assert the native screen is up and warning-free, exercise
  pause/play/fast/finish, retreat a merc and all mercs, and Done; assert the game returns
  to the map and the state changed (a merc retreated/left). Golden screenshots at the
  reference resolutions.
- **Layout audit** at 1080p / 1440p / 4K / 21:9 and UI scale 100/150%.
- **Screenshot proof** on `pr-screenshots`.
