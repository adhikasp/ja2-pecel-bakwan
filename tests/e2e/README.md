# End-to-end tests

Each `*.lua` file here is a script that drives the real game headless, through
its real UI, and checks what happens. See [docs/automation.md](../../docs/automation.md)
for the full API.

| Script | Covers |
|---|---|
| `main_menu.lua` | Menu entries, Preferences and Credits and back |
| `laptop_tour.lua` | New campaign; every laptop program, reading mail, A.I.M. |
| `new_game_to_tactical.lua` | Hire a merc on A.I.M., land in Omerta, save/load round trip |
| `tactical_move_merc.lua` | Select a merc and walk to a clicked tile and back |
| `map_screen_tour.lua` | Map screen: pause, inventory, options, laptop |
| `video_switch.lua` | Runtime video changes (`ja2.setVideo`) through four modes on tactical, map and laptop |
| `video_options.lua` | The Video options screen: change and apply resolution without a restart |
| `legacy_script.txt` | The old line-based `-uitest` format still works |
| `check_determinism.py` | Same script and seed, twice: identical screenshots |
| `check_sessions.py` | Two `ja2ctl` sessions side by side stay independent |

`lib/campaign.lua` has the shared steps (new game, hire from A.I.M., land,
dismiss popups).

## Running

They need the original game data (`game_dir` in your `ja2.json`) and Python 3.

```bash
ctest -L e2e -j8 --output-on-failure                                  # all, from the build directory
python tools/ja2ctl.py run tests/e2e/laptop_tour.lua --isolated       # one, from the repo root
python tools/ja2ctl.py run tests/e2e/laptop_tour.lua --isolated --show   # and watch it
```

`--isolated` gives every run a fresh home directory, so tests never see your
saves or each other's. Screenshots go to `--out` (CTest uses
`<build>/e2e-out/<test>/`); a failing script also leaves `failure.png` there.

## Writing a test

Find things by what they say rather than where they are, and wait for the game
instead of sleeping:

```lua
local campaign = require("lib.campaign")

campaign.newGame()                      -- ends in the laptop, popups closed
ja2.click("Web")
ja2.waitIdle()
ja2.click("A.I.M.")
ja2.waitFor("ASSOCIATION OF INTERNATIONAL MERCENARIES")
ja2.expect(ja2.state().money == 45000, "nothing spent yet")
ja2.screenshot("aim.png")
```

To find out what a screen offers, explore it in a live session:

```bash
python tools/ja2ctl.py start
python tools/ja2ctl.py eval 'require("lib.campaign").newGame()'
python tools/ja2ctl.py ui          # labelled, clickable elements
python tools/ja2ctl.py text        # all visible text with positions
python tools/ja2ctl.py shot        # look at it
```

Things worth knowing:

- **Popups come and go.** First-visit help screens and "You have new mail"
  appear depending on history; use `campaign.dismissHelp()`,
  `campaign.dismissLaptopPopups()` and `campaign.acceptMessageBox(pattern)`.
- **Ambiguous labels.** "History" is both a laptop program and an A.I.M. link:
  use `{text = "History", exact = true, within = {x = 0, y = 0, w = 110, h = 480}}`
  or `index = 2`.
- **Image-only widgets** have no text to find. Give them a name in C++
  (`button->SetName("Close help")`); see docs/automation.md.
- **Waiting.** `ja2.waitIdle()` understands fades, laptop page loads, AIM video
  calls, speech, the helicopter drop and walking. If a new kind of animation
  makes a test flaky, teach `NothingInFlight()` about it rather than adding
  `ja2.wait()`.
- **Pixels** (`ja2.pixelIs`, `ja2.waitPixel`) still work, but depend on the
  resolution; the tests run at 640x480.

## Resolution matrix

`ctest -L resolution` runs every tour at 640x480, 1280x720, 1920x1080,
2560x1080 and 3440x1440. Scripts take screenshots with
`shots.take(name[, golden])` (`lib/shots.lua`), which first calls
`ja2.assertInsideScreen()` (fails if any mouse region or button lies outside the
screen) and can register the shot for comparison with `golden/<res>/`. See
`golden/README.md`. Use `campaign.std{...}` to convert classic 640x480
coordinates (e.g. in `within=`) to the current screen.

## Layers (world zoom)

`tactical_layers.lua` and `tactical_layers_ui.lua` run tactical with the world as a layer of its own:
`ja2ctl run ... --res 2560x1440 --uiscale 2 --worldzoom 1` (headless only splits when `--worldzoom` is
given). Clicks and `ja2.gridPos` are in UI pixels; screenshots are the composite at window size
(UI size * UI scale), so 2560x1440 here. `ctest -L e2e` runs them at three combinations.
