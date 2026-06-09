# E2E Script Cookbook

Reusable, calibrated building blocks for `-uitest` scripts. Paste a recipe into a
new `tests/e2e/<name>.txt` and adjust as needed. For the command vocabulary and
the driver's design, see [../../docs/plan/e2e-screen-automation.md](../../docs/plan/e2e-screen-automation.md).

> **All coordinates and colors here are calibrated at `-res 1280x720`.** Recalibrate
> (see [Calibrating new anchors](#calibrating-new-anchors)) if the resolution, UI
> art, or game version changes.

## Running

```bash
# build once, then run a script
cmake --build build -- -j$(sysctl -n hw.logicalcpu)
./build/ja2 -res 1280x720 -uitest tests/e2e/<name>.txt ; echo "exit=$?"
```

Exit codes: `0` all asserts passed · `1` an assertion failed · `2` setup/parse error
(bad/missing script). In CI, `cd build && ctest -R e2e`.

In `-uitest` mode the game window is created **hidden** and the app runs in the
background, so launching a test never steals focus or pops a window. Screenshots and
pixel asserts still work because they read the CPU-side `ScreenBuffer`, not the GPU.
To watch a run live, temporarily comment out the `SDL_WINDOW_HIDDEN` line in
[../../src/sgp/Video.cc](../../src/sgp/Video.cc) (`InitializeVideoManager`).

## Gotchas worth knowing

- **The intro splash is slow and variable (~10s).** Don't `wait` a fixed amount for
  the main menu — `waitpixel` on a menu anchor (below). A click during the splash
  just skips a frame of it, it does not reach the menu.
- **Screens fade in.** Right after a screen first appears, the whole frame ramps up
  in brightness over ~1–2s. Asserting immediately reads a too-dark pixel. After a
  `waitpixel` detects the screen, add `wait 3000` (or `waitstable`) before asserting.
- **Some elements pulse/blink** (the selected-sector highlight box, cursors). Never
  `assertpixel` on those — pick a *static* element (brass frame, solid panel) for
  assertions. Pulsing elements are still fine as a `waitpixel` *trigger*.
- **Clicks need the cursor already there.** `click` already does move→down→gap→up, so
  prefer it over a bare `move`+`rclick`. Use tolerance 12–20 on dithered art.

## Recipe: wait for the main menu

```
# Blocks until the main menu's gold "START NEW GAME" text is drawn (rides out the
# variable-length intro splash). Use this at the top of almost every script.
waitpixel 640 407 #a48d41 20 25000
```

Main-menu item hit points (all at x≈640):

| Item | y | Click |
|---|---|---|
| START NEW GAME | 407 | `click 640 407` |
| CONTINUE SAVED GAME | 445 | `click 640 445` |
| PREFERENCES | 483 | `click 640 483` |
| CREDITS | 520 | `click 640 520` |
| QUIT | 557 | `click 640 557` |

## Recipe: load a saved game → strategic world map

Full, reliable flow (this is what [load_save_to_mapscreen.txt](load_save_to_mapscreen.txt) does):

```
# 1. main menu
waitpixel 640 407 #a48d41 20 25000

# 2. CONTINUE SAVED GAME -> LOAD GAME screen
click 640 445
waitpixel 620 143 #a4854a 30 10000     # gold "LOAD GAME" title

# 3. pick the first save slot (slots are stacked ~36px apart: y=183,219,253)
click 400 183
wait 600

# 4. Load Game button (Cancel is at 740 571)
click 534 571

# 5. older-save confirm dialog: white "Attention" text -> YES (NO is at 676 402)
waitpixel 640 335 #ffffff 16 8000
click 601 402

# 6. wait for the world map, then let its fade-in settle
waitpixel 779 164 #ffff00 16 40000     # yellow selected-sector box (pulses; trigger only)
wait 3000

# 7. assert on STATIC brass-frame pixels (do NOT assert the pulsing sector box)
assertpixel 375 232 #eee6cd 20
assertpixel 577 280 #eee6cd 20
```

> The "older version" confirm dialog (step 5) appears because the checked-in fixtures
> are old saves. A save made in the current version skips it — guard with the
> `waitpixel`'s timeout (it just falls through after 8s if no dialog) and a harmless
> `click 601 402`.

## Anchor reference (1280x720)

Static = safe for `assertpixel`. Pulses = `waitpixel` trigger only.

| Screen | What | Pixel | Color | Kind |
|---|---|---|---|---|
| Main menu | "START NEW GAME" text | 640,407 | `#a48d41` | static |
| Load Game | "LOAD GAME" title | 620,143 | `#a4854a` | static |
| Load Game | first slot (selected) | 400,183 | `#cdc68b` | static |
| Confirm dialog | "Attention" text | 640,335 | `#ffffff` | static |
| Map screen | selected-sector box (A9 Omerta) | 779,164 | `#ffff00` | **pulses** |
| Map screen | roster-panel brass frame | 375,232 | `#eee6cd` | static |
| Map screen | map-panel brass frame | 577,280 | `#eee6cd` | static |

## Calibrating new anchors

To find a *static* anchor on a new screen (avoids flickering/pulsing elements):

1. Drive to the screen and dump several frames a few hundred ms apart:
   ```
   waitpixel <trigger>
   wait 3000
   screenshot /tmp/r1
   wait 250
   screenshot /tmp/r2
   wait 250
   screenshot /tmp/r3
   ```
2. Find pixels identical across all frames (those are static, not animated):
   ```bash
   python3 - <<'PY'
   from PIL import Image
   ims=[Image.open(f'/tmp/r{i}.bmp').convert('RGB') for i in (1,2,3)]
   for y in range(120,460,2):
     for x in range(315,955,2):
       cs=[im.getpixel((x,y)) for im in ims]
       r,g,b=cs[0]
       if all(c==cs[0] for c in cs) and r+g+b>300:
         print((x,y), '#%02x%02x%02x'%cs[0])
   PY
   ```
3. Cross-check the candidate is a *different* color on other screens (so the assert
   actually proves you're on the intended screen), then use it with tolerance 16–20.
