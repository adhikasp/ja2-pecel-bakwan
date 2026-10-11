---
name: orchestrate-issues
description: Use this skill to drive a whole list of GitHub issues to merged. It works through a milestone, an explicit issue list, a label or a board track, and for each issue spawns an OpenCode agent that implements it end to end, up to one coding agent at a time. Use it when asked to "orchestrate", "drive these issues", "run down the milestone", or to batch through a backlog.
---

# orchestrate-issues

You are the **manager**: board keeper and task issuer. You do not implement
anything yourself — not C++, not tests, not screenshots. Your job is to read the
backlog, decide what is ready to run, hand each issue to an OpenCode agent that
drives it from `open` to `merged`, keep the board honest, and never stop the line
for a question you cannot answer.

Repo rules in `AGENTS.md` always apply: fork `adhikasp/ja2-pecel-bakwan` only,
never upstream; PRs into protected `master`; native-first C++.

## The one rule that shapes everything

**Nobody is watching.** The operator is not at the keyboard. This run must reach
the end of the list unattended. Therefore:

- Never use the `question` tool. Never wait for an answer that will not come.
- A judgement call that only the owner can make does **not** block the run. It
  becomes a labelled, commented issue the operator can answer later, and you
  move on to the next task.
- Anything you would have asked becomes, instead, a written question with
  options on the issue. That is the escalation channel.

## 1. Build the task list

`$ARGUMENTS` is whatever the operator handed you. Normalise it to issue numbers:

| You got | Do |
|---|---|
| A milestone URL or number (`.../milestone/9`, `9`, `"Native Modern Graphic"`) | `gh issue list -R adhikasp/ja2-pecel-bakwan --milestone "<m>" --state open --limit 100 --json number,title,labels` |
| Issue numbers / URLs (`#43 #44`, a pasted list) | use them as given |
| A label or area (`nativeui`, `world-renderer`) | `gh issue list … --label "<label>" --state open` |
| A board track (`Modernize graphics`) | `gh project item-list 1 --owner adhikasp --format json --limit 500`, filter on `Track`, take `Status != Done` |
| Nothing, or "the next milestone" | the open milestone with the most open issues, from `gh api repos/adhikasp/ja2-pecel-bakwan/milestones` |

Then:

1. **Drop `goal` issues from the run list.** A goal is an objective statement and
   a progress ledger, not a slice of work. Note it in the final report as context
   for the milestone's completion, and do not spawn an agent on it.
2. **Drop anything with an open PR against it** (`gh pr list -R adhikasp/ja2-pecel-bakwan --state open --search "<n>"`). Another run already has it.
3. **Order by dependency.** For each issue read its native relations
   `gh api repos/adhikasp/ja2-pecel-bakwan/issues/<n>/dependencies/blocked_by`.
   If any blocker is open, the issue waits. Where no relation is declared, fall
   back to the `Depends on:` / `Gated by:` lines in the body, then to grouping by
   area label so two agents never touch the same subsystem concurrently.
4. Write the ordered list down before you start, with the reason each task sits
   where it does. If a task turns out to be unrunnable, the list tells you what
   comes next.

## 2. Keep your own checkout current

You are reading code and issue state to make decisions, so **your checkout must
not drift**. Every agent you spawn merges its work into `master` behind your
back; ten tasks from now, your copy of the tree would be ten merges stale and
every judgement you make would be about code that no longer exists.

Three moments matter:

**At the start**, before you build the task list:

```bash
cd <repo> && git fetch origin && git merge --ff-only origin/master
```

**Before every worktree**, so the branch point is current:

```bash
git fetch origin
git worktree add ../.ja2-orch/issue-<N> -b issue-<N> origin/master
```

Note this bases the branch on `origin/master`, not on your local `master` — so a
stale or busy local branch can never corrupt an agent's worktree. Re-fetching
is still what makes `origin/master` the newest code.

**After every merge**, before you judge the next task:

```bash
cd <repo> && git fetch origin && git merge --ff-only origin/master
git worktree prune          # drop worktrees whose agent is finished
```

Rules:

- **`--ff-only`, never `reset --hard`.** You have no business discarding
  anything in this checkout. If the merge cannot fast-forward, your checkout has
  local commits you did not make — report it and keep going; the run does not
  stop for it.
- **If the checkout is dirty**, leave it alone and say so once in the final
  report. You can still run the whole list: worktrees branch from
  `origin/master`, not from your tree. You are only reading slightly stale code
  while triaging.
- After `git merge --ff-only`, the build directory here goes stale too. Run
  `python tools/dev.py build` **only** if you intend to build here — you should
  not be, because you do not implement. `sccache` makes the next agent's build
  cheap either way.

## 3. Decide what needs research first

A task that is vague should be researched by a **read-only** agent *before* a
coding agent touches it. Sending a coding agent into an under-specified issue
just burns a build cycle and produces a PR that has to be thrown away.

**Send it to a researcher first when any of these hold:**

- there is no `Done when` / acceptance criteria in the body;
- the body names a *goal* ("richer", "polish", "audit", "proof", "measure",
  "bring up to parity") without saying how;
- the work is a **design decision** — game design, art direction, screen layout,
  visual style. Look at the title and body for: `style`, `directive`, `wireframe`,
  `layout`, `parity`, `polish`, `audit`, and the labels `nativeui`, `laptop`,
  `mapscreen`, `world-renderer`;
- it is labelled `decision` or `question`;
- it cites a plan doc that sets the direction but not the specifics;
- the issue's premise may simply be out of date.

**Skip the researcher when** the body names the files or functions to touch and
gives a checkable "Done when". A researcher there is pure latency.

The researcher (`--agent researcher`, see `.opencode/agents/researcher.md`) is
genuinely read-only — file edits are denied by permission, not by request — and
it delivers a findings comment on the issue ending in:

```
<!-- orchestrator-research -->
state: RESOLVED | NEEDS_DECISION
<!-- /orchestrator-research -->
```

- `RESOLVED` → its comment *is* the brief. Spawn the coding agent and point it at
  that comment.
- `NEEDS_DECISION` → apply the [escalation protocol](#6-escalation-nobody-is-watching)
  and move on. Do not spawn a coding agent on a blocked issue.

## 4. Concurrency

- **Exactly one coding agent at a time.** Two agents editing `master`-derived
  worktrees will conflict, and the shared build directory and `sccache` are sized
  for one.
- **At most two researchers at a time.** They are read-only, so they overlap
  safely and cost little.
- Start researchers for the *next* tasks while the current coding agent runs.
  Never start a second coding agent.

## 5. Spawn an agent

**Every writing agent gets its own worktree.** Never point a writing agent at the
orchestrator's own checkout, and never let two share one.

```bash
git -C <repo> fetch origin
git -C <repo> worktree add ../.ja2-orch/issue-<N> -b issue-<N> origin/master
```

Researchers get a worktree too — a fresh checkout is cheap, and it makes
"read-only" verifiable: after the run, `git status --porcelain` in the worktree
must be empty. If it is not, the agent overstepped; say so in the report.

Write the agent's brief to a file, then use the bundled spawner, which handles
the CLI lookup and avoids all shell-quoting hazards on macOS and Windows alike:

```bash
python .agents/skills/orchestrate-issues/spawn.py \
    --worktree ../.ja2-orch/issue-275 \
    --prompt /tmp/issue-275.md \
    --title "orchestrate #275"
```

`--wait` blocks and prints the log tail; without it the session runs in the
background. **Prefer `--wait` in a background shell task** — a full
implement-test-QA-merge cycle is far longer than any foreground timeout, and you
want the completion notification rather than a poll loop.

Every spawn passes `--model opencode/space-bunny-free` by default; override with
`ORCHESTRATOR_MODEL` or `--model` if you are asked to.

### The coding agent's brief

Give it the `impl-issue` skill and the issue, plus the context only you have:

```markdown
Use the impl-issue skill to take issue #275 from open to merged.

Issue: https://github.com/adhikasp/ja2-pecel-bakwan/issues/275

You are running unattended — there is no interactive user.
- impl-issue step 2 ("stop and ask") becomes: post the question as a comment on
  the issue, add the label `needs-owner-decision`, and stop with a BLOCKED
  report. Never call the question tool. Never guess an owner decision.
- Everything else in impl-issue is unchanged, including the QA rounds, the
  screenshot proof and step 7's merge.

[If research ran:] Research already exists — read the findings comment on the
issue before designing anything. Treat it as the brief.

[Orchestrator notes: blockers already closed, sibling issues running now,
files another agent is touching, decisions already made.]
```

Tell it to finish by posting the machine-readable block below **on the issue**,
so the result survives the process and you can read it without the log:

```
<!-- orchestrator-status -->
state: DONE | BLOCKED | NEEDS_DECISION | SKIPPED
pr: <url or none>
summary: one sentence
<!-- /orchestrator-status -->
```

Read it with `gh issue view <N> -R adhikasp/ja2-pecel-bakwan --comments`.

## 6. Escalation (nobody is watching)

When an agent is blocked on something only the operator can decide, it does this
— and you make sure it happens:

```bash
gh issue comment <N> -R adhikasp/ja2-pecel-bakwan --body-file <file>   # the question, 2-3 options, a recommendation
gh issue edit <N> -R adhikasp/ja2-pecel-bakwan --add-label needs-owner-decision
```

The comment must be answerable in one reading: the question, the options with
their real consequences, and **one recommended option with its reason**. Create
the label once if it does not exist:

```bash
gh label create needs-owner-decision -R adhikasp/ja2-pecel-bakwan \
    --color d93f0b --description "Blocked on a decision only the owner can make"
```

You apply the same treatment to anything *you* cannot decide. Then set the board
item to `Blocked` and continue to the next runnable task. An escalation is a
handoff, never a stop.

When the operator later answers, the answer is a comment on the issue. A later
run picks it up — do not try to wait for it inside this run.

## 7. After each merge

The coding agent merges its own PR (impl-issue step 7). Your job is the audit
that follows:

1. **Confirm it really merged**: `gh pr view <pr> --json state,mergedAt`,
   `gh issue view <N> --json state`. If the PR is open and green, finish it
   yourself rather than re-running the agent.
2. **Look at the screenshots.** The agent is required to post proof on the
   `pr-screenshots` branch. Open the images and actually look at them. This is
   not optional and not a formality: a broken widescreen layout once shipped
   because the goldens froze it and nobody opened a picture. If something is
   visibly wrong, file a follow-up issue with the image, label it `bug`, and
   carry on — do not silently accept it.
3. **Board**: set the item to `Done`, confirm `Track` and `Kind` are set.
   Resolve the ids once and reuse them:
   ```bash
   gh project field-list 1 --owner adhikasp --format json      # Status / Track / Kind ids
   gh project item-edit --id <itemId> --project-id 1 --field-id <statusFieldId> \
       --single-select-option-id <doneOptionId>
   ```
   Add the issue to the board first if it is missing:
   `gh project item-add 1 --owner adhikasp --url <issue-url>`.
4. **Clean up the worktree**: `git worktree remove <path>` once the agent is
   done with it.
5. **Re-sync your own checkout** — do this before you touch the next task, or
   every later judgement you make is about a tree that no longer exists:
   ```bash
   git fetch origin && git merge --ff-only origin/master && git worktree prune
   ```
   See [section 2](#2-keep-your-own-checkout-current) for the `--ff-only` and
   dirty-checkout rules. If the agent merged into a **worktree branch** rather
   than `master`, pull that branch's state before you judge it — a PR that is
   merged upstream but stale in its worktree is still merged upstream, and
   upstream is what counts.

## 8. Keep the board honest as you go

Every issue you pick up goes to `In Progress` before its agent starts, and back
to `Backlog` (never left `In Progress`) if it is skipped or escalated. A board
that lies about what is in flight is worse than no board.

## 9. Finish

When the list is exhausted — merged, skipped or escalated — report:

- **Shipped**: issue → PR, one line each, in the order they landed.
- **Escalated**: issue → the decision you need, with your recommendation. This
  is the section the operator reads first.
- **Skipped**: issue → why (blocked by an open issue, already has a PR, needs a
  decision, agent failed three rounds).
- **Noticed along the way**: pre-existing bugs, flaky tests or stale docs you hit
  — as issue links, not as prose. Open them; do not fix them yourself.

## Gotchas

- **`gh` and PowerShell**: quoting `--jq '.[] | .title'` through PowerShell
  splits on the pipe. Pipe to `ConvertFrom-Json` and format it in PowerShell
  instead.
- **The desktop app's CLI is not on PATH** on Windows. `spawn.py` finds it at
  `%LOCALAPPDATA%\Programs\@opencodedesktop\resources\opencode-cli.exe`; set
  `OPENCODE_CLI` to override.
- **Never run two coding agents against one build directory.** Each worktree
  builds in its own (`_bin` on Windows, `build` on macOS) but they share the
  machine's job semaphore and `sccache`. That is the reason for the "one at a
  time" rule, not just for git safety.
- **A `goal` issue is not a task.** See step 1.
- **Research is not optional for design work.** "Richer leaf pages" is a design
  directive, not a spec. Let the researcher turn it into one before an agent
  writes code against it.
- **Do not let the orchestrator context fill up.** Read issue bodies and
  comments narrowly, keep summaries to a line, and lean on the issue itself as
  the durable record rather than re-reading whole threads.