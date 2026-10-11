<!-- Thanks for the PR. Fill in the sections below. -->

## What this does

<!-- One or two sentences. What changes and why. -->

## Screenshots

**Tick exactly one:**

- [ ] **Visible change** — screenshots attached below (required if anything the player can see changed: rendering, layout, UI, screens, video settings)
- [ ] **No visual change** — build, tests, tooling, docs, CI or data only

<!-- If visible: published with

    python tools/dev.py shots <branch-name> <the .png files>

which pushes them to the `pr-screenshots` orphan branch and prints the links
to paste. One caption per image saying what to look at. Cover the affected
screens at 1280x720 AND at least one widescreen size (1920x1080; add
2560x1080 or 3440x1440 when the layout depends on width).

Use `python tools/ja2ctl.py shot` to capture them from the real build, and open
every PNG before requesting review. Golden images prove a screen didn't
change, not that it looks right. -->

## Checklist

- [ ] `./build/ja2 -unittests` passes (or `_bin\ja2.exe -unittests` on Windows)
- [ ] Game data tweaks went in `src/externalized/` JSON rather than C++ where possible
- [ ] New image-only widgets call `SetName(...)`, and new animations/loading states are in `NothingInFlight()`
- [ ] I have read the screenshots section above and it is correct
