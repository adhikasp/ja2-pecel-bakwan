<!-- Thanks for the PR. Fill in the sections below. -->

## What this does

<!-- One or two sentences. What changes and why. -->

## Screenshots

**Tick exactly one:**

- [ ] **Visible change** — screenshots attached below (required if anything the player can see changed: rendering, layout, UI, screens, video settings)
- [ ] **No visual change** — build, tests, tooling, docs, CI or data only

<!-- If visible: hosted on the `pr-screenshots` orphan branch, one caption per
image saying what to look at. Cover the affected screens at 640x480 AND at
least one widescreen size (1920x1080; add 2560x1080 or 3440x1440 when the
layout depends on width). Embed them as:

![map 1920x1080](https://raw.githubusercontent.com/adhikasp/ja2-pecel-bakwan/pr-screenshots/<branch-name>/map_1920x1080.png)

Use `python tools/ja2ctl.py shot` to capture them from the real build, and open
every PNG before requesting review. Golden images prove a screen didn't
change, not that it looks right. -->

## Checklist

- [ ] `./build/ja2 -unittests` passes (or `_bin\ja2.exe -unittests` on Windows)
- [ ] Game data tweaks went in `src/externalized/` JSON rather than C++ where possible
- [ ] New image-only widgets call `SetName(...)`, and new animations/loading states are in `NothingInFlight()`
- [ ] I have read the screenshots section above and it is correct
