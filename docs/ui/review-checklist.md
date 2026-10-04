# Screenshot and review checklist (native screens)

For the author before opening a PR, and for the reviewer before approving it. It extends
[AGENTS.md](../../AGENTS.md) ("Every PR needs screenshot proof") for the native UI work of
[native-modern-game.md](../plan/native-modern-game.md). Paste the filled-in checklist into the PR description.

## 1. Screenshots to take

Take them from the real game with `python tools/ja2ctl.py` (`shot`, or `ja2.screenshot()` in a tour script), and
host them on the `pr-screenshots` branch as AGENTS.md describes.

- [ ] Every state of the screen from its functional spec (`docs/ui/<screen>.md` §4), including popups (§5)
- [ ] At **1920x1080** (the reference), **2560x1440**, **3840x2160** and **3440x1440** (21:9)
- [ ] At **1280x720** (the minimum layout size)
- [ ] At UI scale **100%, 125%, 150% and 200%** for at least the default state
- [ ] One long-string language (German or Russian) for the text-heaviest state
- [ ] Hover, pressed, focused and disabled states of anything interactive the PR adds (tooltips visible)
- [ ] For world rendering changes: the legacy output, the new output and the pixel diff image (magenta = different),
      with the diff percentage in the caption
- [ ] Each image has a caption saying what to look at

## 2. The author looks at every image

Open each PNG (not a thumbnail) and check:

- [ ] Nothing is clipped, cut off at the window edge, overlapping or unexpectedly empty
- [ ] Text is crisp at every scale (no pixel-doubled or blurry text), and truncated text has an ellipsis and a tooltip
- [ ] Alignment and spacing follow the design system tokens (`docs/ui/design-system.md` once Phase 1 lands)
- [ ] Colours: status colours are distinguishable for colour-blind players (not colour alone)
- [ ] The layout uses the extra space at wide/tall sizes as the wireframe intended (what grows, what scrolls, what docks)
- [ ] No leftovers of the legacy screen (640x480 box, letterbox filler, old bitmap font)
- [ ] Headless rendering artefacts are called out (e.g. SDL's software rasteriser draws hairlines on rounded
      borders; see native-modern-game-decisions.md) and a GPU capture is attached if they hide something

## 3. Behaviour

- [ ] The parity tour for the screen passes (`ctest -L e2e`), and its assertions check game state, not only pixels
- [ ] Every row of the functional spec is covered by the tour or listed under "deliberately dropped"
- [ ] Keyboard shortcuts work and are shown in the UI (tooltips or labels)
- [ ] The layout audit (`ja2.assertInsideScreen()` and its native extension) passes at every size above
- [ ] Elements are addressable by id for automation (`ja2.ui`, `ja2.click{id=...}`)
- [ ] `ctest -L e2e`, `ctest -R resolution_` and `ja2 -unittests` pass; performance budget (UI ≤ 2 ms per frame at
      4K on the GPU path) is measured and stated

## 4. The reviewer

- [ ] Opened every image in the PR description; any missing or wrong image sends the PR back
- [ ] Compared at least one state side by side with the legacy screen at 1920x1080
- [ ] Checked the functional spec's "deliberately dropped" list and agrees with each item
- [ ] Style decisions (colours, typography, layout direction) were approved by the project owner, not made in review
