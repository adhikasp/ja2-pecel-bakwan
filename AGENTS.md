# Agent guidelines

Build, test and automation instructions are in [CLAUDE.md](CLAUDE.md) and [docs/automation.md](docs/automation.md).

## Every PR needs screenshot proof

A PR that changes anything the player can see (rendering, layout, UI, screens, video settings) must show screenshots of the result in its description. Passing tests is not enough. Golden images only prove a screen didn't *change*, not that it looks *right*.

1. **Take them from the real game.** Drive the build with `python tools/ja2ctl.py` (or a `--show` window) and use `ja2ctl shot`.
2. **Look at every one before opening the PR.** Open each PNG and check it. If a screen looks wrong, fix it; don't explain it away.
3. **Cover the affected screens at 640x480 and at least one widescreen size.** For example 1920x1080, plus 2560x1080 or 3440x1440 when the layout depends on width. When a PR adds or regenerates golden images, include the widescreen goldens it changes.
4. **Host them on the `pr-screenshots` branch.** It is an orphan branch that only holds images, so they never enter `master`'s history:
   ```bash
   git fetch origin pr-screenshots || true
   git worktree add ../pr-shots pr-screenshots 2>/dev/null || (git worktree add --detach ../pr-shots && cd ../pr-shots && git checkout --orphan pr-screenshots && git rm -rf . -q)
   mkdir -p ../pr-shots/<branch-name> && cp <shots>.png ../pr-shots/<branch-name>/
   cd ../pr-shots && git add . && git commit -m "Screenshots for <branch-name>" && git push origin pr-screenshots
   ```
   Embed them in the PR body with
   `![map 1920x1080](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/<branch-name>/map_1920x1080.png)`.
   Give each one a short caption saying what to look at.
5. **Reviewers open the images.** Don't merge a UI PR without doing so. If an image is missing or shows a problem, send the PR back.

A PR with no visible change (build, tests, tooling) says "No visual change" instead.
