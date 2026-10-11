---
name: impl-issue
description: Implement a GitHub issue end to end — read the whole issue and prior research, ask the user about judgement calls, open a PR, have a QA subagent review the end product, iterate until good, fix pre-existing problems met along the way, then merge. Use when the user says "impl <issue link or #N>".
argument-hint: <github issue URL or #number>
---

# impl-issue

Take issue `$ARGUMENTS` from "open" to "merged". Repo rules in `AGENTS.md` always apply (fork `adhikasp/ja2-pecel-bakwan` only, never upstream; PR into protected `master`; native-first C++; screenshot proof for visible changes).

## 1. Read everything first

- `gh issue view <n> -R adhikasp/ja2-pecel-bakwan --comments` — the **full** body and **every** comment. Prior research, decisions and review notes live in the comments; do not skim.
- Follow every `#N`, PR link and `docs/plan/` / `docs/ui/` doc the issue cites. Read linked issues' bodies when they are `Depends on` / `Gated by` edges.
- Check dependencies with `gh issue view` (blockedBy). Check that blockers are closed and the code they promise is actually on `master`.
- Read the code the issue touches before designing anything.

## 2. Stop and ask when it is the user's call

Use AskUserQuestion — batched, with a recommended option — before writing code if any of these hold:

- The issue is a `decision` / `goal` / `question`, or has an unresolved blocker.
- Scope is ambiguous, the research in the issue conflicts with itself or with the code, or there are 2+ viable designs with different gameplay/UX consequences.
- A screen/layout needs owner approval (see memory: owner approves every screen wireframe).
- You'd need to touch frozen layers (`rust/`, `assets/externalized/`) or deviate from the design principles.

Don't ask about things with a conventional default or that the code answers. Ask early, not after the PR is open.

## 3. Implement

1. `gh issue edit`/board: set Status `In Progress`. Work in a worktree/branch off up-to-date `master` (`python tools/dev.py setup` in a fresh worktree).
2. Follow the layered shape from AGENTS.md: gtest-covered core → thin adapter → `Observable` at decision points → small Lua surface. Lua only drives/tests.
3. Loop short: `python tools/dev.py build`, `test`, then `e2e` for integration. Assert results through `ja2ctl state/eval`, not screenshots.
4. **Pre-existing problems**: if you hit a bug, warning, flaky test or stale doc in the area you touch, fix it in the same PR (mention it under "Also fixed"). If it is large and unrelated, open a separate issue and say so instead of bloating the PR.

## 4. Open the PR

- Commit, push to `origin`, `gh pr create -R adhikasp/ja2-pecel-bakwan` with `Closes #N`, the issue's milestone, and a body covering: what changed, how it was tested, "Also fixed".
- Visible change → screenshots at 1280x720 + a widescreen size from the real game via `ja2ctl shot`, hosted on the `pr-screenshots` branch and embedded (AGENTS.md "Every PR needs screenshot proof"). **Look at every image yourself.** No visible change → write "No visual change".
- Bind the PR with the ccd_pr tools if the desktop app is in use.

## 5. QA subagent review (end product, not code)

Spawn a **fresh** subagent (Agent tool, general-purpose) per round. It must judge the *shipped behaviour*, not read the diff for style. Give it:

- the issue text + acceptance criteria / definition of done, and the PR link;
- the instruction to build the branch and drive the real game with `python tools/ja2ctl.py` (start, `ui`, `text`, `click`, `shot`, `state`), exercising every acceptance criterion as a player would, at 1280x720 and one widescreen size;
- to *look at* each screenshot and report layout glitches, clipped/overlapping text, dead buttons, missing states, regressions in neighbouring screens, crashes, log errors;
- to return: PASS / FAIL, a numbered list of defects with repro steps and screenshot paths, and what it could not verify. It must not edit code.

Do not pass it your own opinion that it's fine.

## 6. Iterate

For each defect: fix, rebuild, re-run `test` + relevant `e2e`, push, refresh screenshots, then run a **new** QA round. Repeat until a round returns PASS with no open defects. After ~3 rounds without convergence, or if QA surfaces a design question: stop and ask the user (AskUserQuestion) with the evidence; do not merge.

## 7. Merge

Only when **all** are true:

- the latest QA round is PASS;
- `python tools/dev.py test` and the relevant e2e / `ctest -L resolution` pass locally;
- GitHub CI on the PR is green (`gh pr checks`; use the ccd_pr tools to read CI — don't poll yourself);
- no unresolved review comments; branch is current with `master` (sync, re-test if conflicts).

Then `gh pr merge --squash --delete-branch -R adhikasp/ja2-pecel-bakwan`, set the board item to `Done`, confirm the issue closed, and clean up the worktree.

## 8. Report

Short summary to the user: PR link, what shipped, QA rounds and what they caught, pre-existing issues fixed, anything deliberately left out (with issue links).
