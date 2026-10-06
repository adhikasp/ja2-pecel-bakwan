# Weapon readout — range and ballistics (issue #141)

| | |
|---|---|
| Native code | `src/game/NativeUI/WeaponReadout.cc` (the `WeaponReadoutViewModel`, "readout"), `assets/ui/screens/weapon_readout.rml`/`.rcss` |
| Core | `src/game/Equipment/WeaponBallistics.{h,cc}` (pure, unit-tested: `WeaponBallistics_unittest.cc`) |
| Opened from | the tactical HUD detail panel (the chart button) and the map screen toolbar (both call `NativeUI::OpenWeaponReadout`); `ja2.debug("readout")` in automation |
| Closes to | whatever it was opened over (it is an overlay, not a screen with its own `ScreenID`) |
| Design | [equipment-revamp.md](../plan/equipment-revamp.md), rule 5 "readout before commit" |
| Status | implemented |

## Why

The damage pipeline (#260) resolves a shot as a pure function of weapon × ammo × range ×
armour × condition, but nothing showed the player what it decided. This screen makes NCTH
legible before the trigger: the range/ballistics readout, and a side-by-side comparison of
two weapons, both read straight from the pipeline.

## What it shows

Compact and decision-first: per range band (4, 8, 12, 16, 24, 32 tiles), for each of the two
weapons:

| Column | Meaning |
|---|---|
| Hit | the chance the roll is taken against, after range and condition |
| Dmg | the damage that reaches the body, after the armour's absorption |
| Thru | penetration − threshold: "through by N", or a negative number when the armour held (a dash with no armour) |
| Noise | the muzzle noise, after the ammo's share and any suppressor |

The better value on each axis is highlighted. A verdict line counts the axes each weapon wins.
The target armour tier is a selector (none / soft / plate); each weapon slot has its own ammo
type (ball / AP / hollow point / subsonic), so the counters can be read directly.

The two slots default to the selected merc's hand gun and the next gun the squad carries; the
`Change` button cycles through every gun in the squad's inventory, and automation can set any
gun or ammo by name.

## Where the numbers come from

`ComputeReadout` calls `ResolveShot` once per band with a reference shooter (70% aim) and a
fresh target (full armour condition); `roll = 0` means the row shows what a connecting round
does, and the `Hit` column is the odds it does. The weapon profile is built by
`WeaponProfileFor` from the game's own data (`ubImpact`, `GunRange`/10, `ubAttackVolume`, the
item's reliability and a fitted suppressor). That bridge is the interim mapping until the
compiled catalog (#100) owns per-weapon penetration; its invariants are unit-tested so #100
changes numbers, not behavior.

## Test surface

- `WeaponBallistics_unittest.cc`: the readout shape, range falloff, the ammo counters, the
  comparison, and the bridge invariants.
- `tests/e2e/weapon_readout.lua`: opens the readout over tactical and the map screen, sets two
  weapons and ammo, and asserts the pipeline's numbers through the `readout` view model
  (`ja2.viewModel("readout")`), then takes the layout/golden screenshot. Below 1280x720 the
  native UI is unavailable and the test only layout-checks the screen it is on.

## Deliberately not here

- No damage pipeline change and no weapon catalog: this is a view over what #260 and the
  game's data already decide.
- The comparison is two weapons at one armour tier at a time; there is no "best weapon for
  this target" ranking.
- The in-loadout presentation of the readout is [#262](https://github.com/adhikasp/ja2-pecel-bakwan/issues/262)'s
  screen, which reuses the same view model and core.
