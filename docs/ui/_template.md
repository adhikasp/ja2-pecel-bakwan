# <Screen name> — functional spec (M1)

> Copy this file to `docs/ui/<screen>.md` before redesigning a screen (see *Per-screen methodology* in
> [native-modern-game.md](../plan/native-modern-game.md)). This spec is the **parity contract**: everything the
> legacy screen does is listed here, and the native screen ships when every row is covered by the parity tour
> (M4) or is marked as deliberately dropped, with a reason.

| | |
|---|---|
| Legacy code | `src/game/...` (entry: `Enter...()`, handler: `...Handle()`, exit: `Exit...()`) |
| Screen id | `..._SCREEN` (what `ja2.screen()` returns) |
| Reached from | e.g. main menu "Load Game", map screen `alt+l`, ... |
| Returns to | |
| Owner of this spec | |
| Status | draft / reviewed / parity-complete |

## 1. How it was audited

- Automation dump of every state: `python tools/ja2ctl.py ui --all`, `text`, and screenshots at 640x480 and
  1920x1080, for each state in §4. Link the tour script used: `tests/e2e/<screen>_audit.lua`.
- Code read: list the functions and files read, and anything surprising found in them.
- Image usage for this screen (from the asset usage map, `tools/assets/usage.py`): the images it loads and what
  each one shows.

## 2. Information shown

Every piece of information the player can read or infer, including what is only shown by colour, icon or position.

| # | Information | Source (game state / function) | Shown as (legacy) | Shown when | Notes |
|---|---|---|---|---|---|
| I1 | | `gMercProfiles[...]` | text / icon / colour / bar | always | |

## 3. Actions

Every way to change something: mouse (left, right, double, drag, wheel), keyboard, and implicit actions (timers,
hovering). Include the game function each one calls: the native screen calls the same one through its view model.

| # | Action | Input (legacy) | Game function | Preconditions / disabled when | Feedback (sound, animation, message) |
|---|---|---|---|---|---|
| A1 | | click "…" / key `x` | `...()` | | |

## 4. States and modes

Every mode the screen can be in, how you get in and out, and what changes (what is shown, what is enabled).

| # | State | Entered by | Left by | Differences |
|---|---|---|---|---|
| S1 | default | | | |

## 5. Popups, overlays and modals

| # | Popup | Opened by | Contents | Buttons / result |
|---|---|---|---|---|

## 6. Sounds, animations and timing cues

| # | Cue | When | Asset | Must stay? |
|---|---|---|---|---|

## 7. Edge cases

Check each and write down what the legacy screen does (a screenshot or a sentence):

- [ ] Empty: no mercs / no saves / no items / nothing selected
- [ ] Full: 20 mercs, 18 saves, the longest names, the largest numbers (money, days)
- [ ] Vehicles, EPCs, the robot, dead mercs, mercs in transit, mercs in another sector
- [ ] During combat / time compression / paused
- [ ] Long translated strings (German, Russian, Polish): which labels overflow today?
- [ ] 640x480 and the widest supported window (32:9): anything that depends on the resolution
- [ ] Keyboard-only use
- [ ] Anything else specific to this screen:

## 8. Deliberately dropped or changed

| Item | Decision | Reason | Approved by |
|---|---|---|---|

## 9. Parity tour

The M4 tour script (`tests/e2e/<screen>_parity.lua`) and which rows it covers:

| Row | Covered by (step / assertion) |
|---|---|
| I1, A1 | |
