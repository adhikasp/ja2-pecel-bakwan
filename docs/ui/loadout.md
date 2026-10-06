# Loadout — pockets, weapon platform and weight (issue #262)

| | |
|---|---|
| Native code | `src/game/NativeUI/LoadoutScreen.cc` (the `LoadoutViewModel`, "loadout"), `assets/ui/screens/loadout.rml`/`.rcss` |
| Core | `src/game/Equipment/LoadoutModel.{h,cc}` (weight band, ammo order, drag keys; unit-tested), the fit rules in `PocketRules`/`AttachmentRules`/`Slots` |
| Opened from | the tactical detail panel's inventory button (`tac.detail.loadout`) and the map screen gear panel's Loadout button (`map.inv.loadout`); `ja2.debug("loadout")` in automation |
| Closes to | whatever it was opened over (it is an overlay, not a screen with its own `ScreenID`) |
| Design | [equipment-revamp.md](../plan/equipment-revamp.md), "the loadout is the plan" |
| Status | implemented; layout approved via the PR screenshots (paperdoll) |

## Why

The tactical HUD's inventory is click-to-pick, click-to-put: it shows the slots but not the
system behind them, and the deep loadout (typed pockets, a weapon platform with typed
attachment slots, magazines and weight) deserves a screen that shows it at once. The
paperdoll is the at-a-glance answer to the issue's open question: every slot is visible
without scrolling, grouped the way the rules group them.

## What it shows

- **The merc**: face, name, screen it came from, and the live load readout — carried weight
  against the carry capacity (metric or pounds, as the game's setting says), the percentage
  and its band (Light / Loaded / Heavy / Overloaded). The percentage is the game's own
  `CalculateCarriedWeight`; the stamina model behind it is [#140](https://github.com/adhikasp/ja2-pecel-bakwan/issues/140).
- **The paperdoll**: face gear 1 and 2, helmet, vest, legs, the two hands. Each slot shows the
  item's picture at a whole-number scale, its condition bar, a stack count and an attachment
  marker.
- **The three LBE windows**, vest / belt / pack: the worn item and the four pockets it
  provides, each labelled with its kind (small / medium / large / magazine, or "none" when no
  item is worn). This is where a pocket's typeness is visible before something is dropped in it.
- **The weapon platform** in hand: the picture, condition and the loaded magazine
  (type and rounds), then one row per typed slot the platform offers — optic, muzzle,
  underbarrel, side rail — with the role and, when empty, the mount it takes.
- **The readout of the fitted weapon** (issue #141's numbers): effective range, muzzle noise,
  condition and the per-band Hit / Dmg / Thru table at a target-armour selector (none / soft /
  plate), all read from the damage pipeline. **Compare** opens the full readout overlay of
  #141 with the same core.

## How the moves work

Every move goes through the legacy rules, never around them, so nothing on this screen can be
done that the game would refuse elsewhere:

| Move | Through |
|---|---|
| Slot to slot (with stacking, swapping and the two-handed rules) | `CanItemFitInPosition` + `PlaceObject`, on a hold-first transaction so a refusal changes nothing |
| An item onto a platform slot, or onto worn armour (plates on a vest, NVG on a helmet) | `CanAttachAt`/`CanAttach` first, then `AttachObject` |
| An attachment off the weapon into a pocket | `RemoveAttachment` + `PlaceObject`; a refusal re-attaches it |
| Swap magazine | `AutoReload` |
| Swap ammo type | the carried magazines of the gun's calibre, in the canonical order (ball, AP, super AP, hollow point), then `ReloadGun` |
| Fill magazines | `SectorStock::FillMagazines()` (issue #124) when the selected sector can run it |

The reason a drop is refused is the rules' own plain-language string (`Equipment::Describe`),
shown on the hint line while the item is held. Removing an attachment no longer renumbers the
platform's other attachments: position *is* the role slot (#96's schema), so taking the scope
off leaves the suppressor in the muzzle slot.

## Interaction

- Click an item to pick it up, click a slot to put it down; or press and drag. The source slot
  stays highlighted and a ghost follows the mouse while something is held; dropping it back
  where it came from puts it down.
- While something is held, every slot it could go to highlights (brass) and every slot that
  would refuse it highlights red; hovering one shows the reason on the hint line.
- The footer hint line is the screen's voice: the default prompt, the item under the mouse
  with its weight, "Drop it here", or the rule's refusal.
- There is no keyboard shortcut: the Close button (or the `close` command) closes the overlay;
  Esc is not routed to overlays in the native UI yet.

## Test surface

- `LoadoutModel_unittest.cc`: the weight bands and text, the ammo order and the drag keys.
- `tests/e2e/loadout_screen.lua` (`e2e_loadout_screen`, resolution sweep goldens
  `1920x1080/loadout_screen/loadout.png` and the matrix): opens the screen from the tactical
  detail panel and from the map screen gear panel, asserts the fitted platform and its typed
  slots, moves the medkit between pockets, refuses a medkit in a magazine pocket with the
  pocket rule's words, detaches and refits the scope, runs the three quick actions by
  click-by-label, opens #141's readout from Compare, and takes the layout/golden screenshot.
  Below 1280x720 the native UI is unavailable and the test only layout-checks the legacy screen.

## Deliberately not here

- No stash grid: the map screen's sector inventory (#124) remains the place to move items
  between the world and a merc. This screen is the merc's own loadout, with the stash actions
  that are about the loadout ("fill magazines").
- No character stats, contract or assignments: those are the tactical detail panel's and the
  map screen's.
- Moving items onto another merc, dropping to the ground and giving are not in this screen yet
  (the legacy cursor still does them).
