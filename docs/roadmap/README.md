# Roadmap

`roadmap.html` is a **generated view of the repository's issues**, never a second
copy of them. The issues are the source of truth; if this page and GitHub ever
disagree, GitHub is right.

## The page is not in the repo

`roadmap.html` and `roadmap.json` are **gitignored generated output**. They are
not committed, not reviewed in a PR, and not part of `master`'s history — the
same reasoning as the `pr-screenshots` branch. Only this README is.

The published copy lives on the orphan `docs-site` branch:
**https://adhikasp.github.io/ja2-pecel-bakwan/roadmap/**, refreshed nightly and
on demand by [`.github/workflows/roadmap-page.yml`](../../.github/workflows/roadmap-page.yml).
It carries its own data-as-of stamp and issue count, so a stale page is obvious
at a glance.

## Generating it

```bash
python tools/dev.py roadmap            # refresh from GitHub (needs `gh`), write both files
python tools/dev.py roadmap --offline  # re-render from the snapshot of the last run
```

Output is deterministic: the same input produces a byte-identical file, so a
regeneration diff means something changed on GitHub. The freshness stamp it shows
is the newest issue *edit*, not the wall clock.

The generator is [`tools/roadmap.py`](../../tools/roadmap.py) with its page
template in [`tools/roadmap_page.html`](../../tools/roadmap_page.html) — **edit
those, never the generated output.** Pure stdlib: the emitted page is one
self-contained file with the issue data inlined, no CDN, no build step, and no
subresource requests, so it opens from `file://` with no network.

Edges are GitHub's native issue relations (`blockedBy` / `blocking`), set with
`python tools/dev.py dep <issue> --blocked-by "#1,#2"`. The `Depends on` /
`Gated by` / `Feeds` / `Unblocks` lines in issue bodies stay as human-readable
annotation; the page reads them only for the nuance a single relation cannot
carry — whether a blocker is a work item or a gate — never to invent an edge.
See [AGENTS.md](../../AGENTS.md#dependencies-between-issues).