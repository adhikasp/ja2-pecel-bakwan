#!/usr/bin/env python3
"""roadmap.py - generate the dependency view over this repo's issues.

GitHub groups issues by milestone and the board groups them by track, but
neither draws the interdependency between tasks, so "what is blocking me" and
"what does this gate" have to be answered by reading issue bodies by hand.
This pulls the backlog and emits one self-contained HTML page with three
linked views of the same graph:

  * the dependency DAG (nodes coloured by track, layered left to right),
  * milestone lanes with the cross-milestone edges drawn over them,
  * the critical path and the bottleneck issues of the open subgraph.

It is a **generated view of the issues, never a second copy**. Edges come from
GitHub's native issue relations (`blockedBy` / `blocking`), which
`python tools/dev.py dep <issue> --blocked-by #1,#2` writes. The prose in issue
bodies stays as human-readable annotation and is parsed only for the nuance the
single relation cannot carry - whether a blocker is a work item or a gate -
never to invent edges.

    python tools/roadmap.py                 # refresh from GitHub, regenerate
    python tools/roadmap.py --offline       # re-render from the last run's snapshot
    python tools/roadmap.py check           # report prose/relation drift
    python tools/roadmap.py print-edges     # the dependency edges, as text
    python tools/roadmap.py backfill        # set the relations the prose states

Both files it writes (`docs/roadmap/roadmap.html` and `roadmap.json`) are
gitignored: they are generated output, so they stay out of `master` and the
published copy lives on the `docs-site` branch.

Pure standard library, no third-party imports: the repo runs on `uv` with
Pillow/numpy for the golden-image tests and this must not add to that. No
vendored graph library either - the page's layout is written by hand.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OWNER = os.environ.get("JA2_ROADMAP_OWNER", "adhikasp")
REPO_NAME = os.environ.get("JA2_ROADMAP_REPO", "ja2-pecel-bakwan")
PROJECT_NUMBER = int(os.environ.get("JA2_ROADMAP_PROJECT", "1"))
PROJECT_OWNER = os.environ.get("JA2_ROADMAP_PROJECT_OWNER", OWNER)
SLUG = f"{OWNER}/{REPO_NAME}"

ROADMAP_DIR = REPO / "docs" / "roadmap"
SNAPSHOT = ROADMAP_DIR / "roadmap.json"
PAGE = ROADMAP_DIR / "roadmap.html"
TEMPLATE = REPO / "tools" / "roadmap_page.html"

ISSUE_FIELDS = ("number,title,state,url,labels,milestone,body,closedAt,updatedAt,"
                "blockedBy,blocking")


class GhError(RuntimeError):
    pass


def log(msg: str):
    print(f"roadmap: {msg}", flush=True)


def warn(msg: str):
    print(f"roadmap: warning: {msg}", flush=True)


def die(msg: str):
    print(f"roadmap: error: {msg}", file=sys.stderr, flush=True)
    raise SystemExit(1)


# --- gh ---------------------------------------------------------------------

def gh(args: list[str]) -> str:
    """Run a gh command and return stdout, or raise GhError."""
    exe = shutil.which("gh")
    if not exe:
        raise GhError("gh not found on PATH")
    proc = subprocess.run([exe] + args, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        raise GhError((proc.stderr or proc.stdout).strip() or f"gh {' '.join(args)} failed")
    return proc.stdout


def gh_run(args: list[str], capture: bool = True) -> subprocess.CompletedProcess:
    exe = shutil.which("gh")
    if not exe:
        raise GhError("gh not found on PATH")
    return subprocess.run([exe] + args, capture_output=capture, text=True,
                          encoding="utf-8", errors="replace")


# --- the prose annotations --------------------------------------------------

REF_RE = re.compile(r"(?<![\w#])#(\d+)\b")
# "PRs #14-#20" names pull requests, which share the numbering with issues but
# are not part of the dependency graph - "(PRs #14-#20), merged to master" is a
# delivery note, not something #14 blocks.
PR_REF_RE = re.compile(r"(?i)\b(?:prs?|pull requests?|merges?|mrs?)\s*:?\s*(?:#\d+|\d+\s*(?:to|-|–)\s*\d+)")

# Verbs that mean "I am waiting for them". A "Gated by" is a blocker too, but a
# gate (a fixture, a proof, a decision) rather than a work item - the page styles
# the two differently, because the wait is different in kind.
BLOCK_VERBS = (
    ("depends on", "block"),
    ("depends upon", "block"),
    ("dependent on", "block"),
    ("blocked by", "block"),
    ("gated by", "gate"),
    ("requires", "block"),
    ("prerequisite", "block"),
    ("prerequisites", "block"),
    ("builds on", "block"),
    ("built on", "block"),
    ("follows", "block"),
)
# Verbs that mean "they are waiting for me" - the same edge seen from the other
# end, so it is recorded as `blocking` rather than as an incoming edge.
FEED_VERBS = (
    ("unblocks", "feed"),
    ("unblock", "feed"),
    ("feeds", "feed"),
    ("consumed by", "feed"),
    ("used by", "feed"),
    ("enables", "feed"),
    ("precedes", "feed"),
    ("gates", "feed"),
)

VERBS = sorted(BLOCK_VERBS + FEED_VERBS, key=lambda kv: -len(kv[0]))
KIND_OF = dict(VERBS)


def _direction(verb: str) -> str:
    return "blocking" if verb in dict(FEED_VERBS) else "blocked"


def _verb_at(text: str, pos: int):
    """The (verb, kind, direction) of the dependency verb starting exactly at `pos`."""
    lowered = text.lower()
    for verb, kind in VERBS:
        if not lowered.startswith(verb, pos):
            continue
        before = lowered[pos - 1] if pos else " "
        after = lowered[pos + len(verb)] if pos + len(verb) < len(lowered) else " "
        if before.isalnum() or after.isalnum():
            continue
        return verb, kind, _direction(verb)
    return None


# A dependency verb only speaks for the refs after it when it *opens* a line:
# `**Depends on:** #96, #260`, `- Feeds #103`, or a heading that is nothing but
# the verb (`## Depends on`). A verb mid-sentence ("the build needs single-digit
# GiB", "rerun after the goldens") is prose, not an edge - guessing there would
# put nonsense into the graph, so those refs stay mentions and the backfill
# judges them by hand.
LINE_OPEN = re.compile(
    r"^(?:>\s*)*(?:[-*+]\s*(?:\[[ xX]\]\s*)?|\d+[.)]\s+)?(?:\*\*|__)?\s*"
    r"(" + "|".join(re.escape(v) for v, _ in VERBS) + r")"
    r"\s*(?:\*\*|__)?\s*:?", re.I)
# A second labelled verb can share the line with the first:
# `**Depends on:** #96, #260 (inputs). **Feeds:** #220, #108.` Each `**Verb:**`
# governs the refs after it until the next one.
BOLD_LABEL_RE = re.compile(
    r"(?:\*\*|__)\s*(" + "|".join(re.escape(v) for v, _ in VERBS) + r")\s*(?:\*\*|__)?\s*:", re.I)
HEADING_RE = re.compile(r"^\s*(#{1,6})\s+(.*?)\s*#*$")
QUOTE_RE = re.compile(r"^(?:>\s*)+")
SEGMENT_REF_RE = re.compile(r"\s*\(?\s*#\d")
RANGE_RE = re.compile(r"#(\d+)\s*(?:to|-|–|through|\.\.)\s*#?(\d+)")
MAX_RANGE = 40  # "#102 to #108" is a list; "#1 to #250" is a number, not a range


def strip_code(body: str) -> str:
    """Blank out fenced and inline code so refs inside it are not annotations."""
    body = re.sub(r"```.*?```", lambda m: re.sub(r"[^\n]", " ", m.group(0)), body, flags=re.S)
    return re.sub(r"`[^`\n]*`", lambda m: " " * len(m.group(0)), body)


def _refs(text: str) -> list[tuple[int, int, int]]:
    """(number, start, end) for each issue ref, minus the pull-request ones."""
    skip = [(m.start(), m.end()) for m in PR_REF_RE.finditer(text)]
    out = []
    for m in REF_RE.finditer(text):
        if any(a <= m.start() < b for a, b in skip):
            continue
        out.append((int(m.group(1)), m.start(), m.end()))
    return out


def _range_targets(line: str) -> list[int]:
    """The issue numbers the `#102 to #108` shorthand stands for."""
    skip = [(m.start(), m.end()) for m in PR_REF_RE.finditer(line)]
    out: list[int] = []
    for m in RANGE_RE.finditer(line):
        if any(a <= m.start() < b for a, b in skip):
            continue
        a, b = int(m.group(1)), int(m.group(2))
        if 0 < b - a <= MAX_RANGE:
            out += [n for n in range(a, b + 1) if n != a]
    return out


def line_verbs(line: str) -> list[tuple[int, str, str, str, int]]:
    """The dependency verbs on a line: (after_pos, verb, kind, direction, scope_end).

    The opening one (a bullet that starts with the verb) plus every `**Verb:**`
    label further along, so a line can switch direction halfway:
    `**Depends on:** #96. **Feeds:** #220, #108.`
    """
    events: list[tuple[int, str, str, str, int]] = []
    opening = LINE_OPEN.match(line)
    if opening:
        verb = opening.group(1).lower()
        events.append([opening.end(), verb, KIND_OF[verb], _direction(verb), len(line)])
    for m in BOLD_LABEL_RE.finditer(line):
        verb = m.group(1).lower()
        events.append([m.end(), verb, KIND_OF[verb], _direction(verb), len(line)])
    events.sort()
    # A gate names its fixture or proof and stops there: in
    # `**Gated by:** #263 (fixtures), balanced against #260 and #102`, #263 is the
    # gate and #260/#102 are ordinary blockers named in the next clause.
    for e in events:
        if e[2] != "gate":
            continue
        rest = line[e[0]:]
        end = len(line)
        walked = 0
        for i, segment in enumerate(rest.split(",")):
            if i and not SEGMENT_REF_RE.match(segment):
                end = e[0] + walked + 1  # keep the comma with the gate's clause
                break
            walked += len(segment) + 1
        e[4] = end
    return [tuple(e) for e in events]


def governing(events, pos: int):
    """The verb that speaks for the ref at `pos`, or None."""
    for after, verb, kind, direction, scope in reversed(events):
        if after <= pos:
            if kind == "gate" and pos >= scope:
                continue  # the gate's own list ended before this ref
            return verb, kind, direction
    return None


def parse_annotations(body: str) -> tuple[list[dict], list[int]]:
    """Edges implied by the prose in a body, and the refs that stay prose.

    A ref is an edge when a dependency verb opens the line it is on:

        **Depends on:** #96, #260 (armor tiers are inputs). **Feeds:** #220
        ## Builds on / depends on
        - #101 (gear catalog)

    Everything else - "Absorbed from #144", "the build needs single-digit GiB" -
    stays a mention: too ambiguous to graph. The page shows those as a faint
    count, so the gaps stay visible instead of being silently dropped.
    """
    text = strip_code(body or "")
    edges: list[dict] = []
    mentions: list[int] = []

    context = None  # (verb, kind, direction) carried by a heading
    for raw in text.splitlines():
        line = QUOTE_RE.sub("", raw)
        heading = HEADING_RE.match(line)
        if heading:
            # A short heading that is nothing but a verb (`## Depends on`,
            # `## Builds on / depends on`) governs the list under it. A longer
            # one is a title that merely contains the word.
            title = heading.group(2).strip()
            found = _verb_at(title, 0) if len(title) <= 60 else None
            context = found
            mentions += [n for n, _, _ in _refs(title)]
            continue

        events = line_verbs(line)
        for num, start, _end in _refs(line):
            gov = governing(events, start) or context
            if gov is None:
                mentions.append(num)
                continue
            verb, kind, direction = gov
            edges.append({"target": num, "verb": verb, "kind": kind,
                          "direction": direction, "line": line.strip()})
        for num in _range_targets(line):
            verb, kind, direction = governing(events, len(line)) or context \
                or ("depends on", "block", "blocked")
            edges.append({"target": num, "verb": verb, "kind": kind,
                          "direction": direction, "line": line.strip()})

    # Deduplicate: the same pair is often named twice in one body.
    seen: set[tuple[int, str]] = set()
    uniq: list[dict] = []
    for e in edges:
        key = (e["target"], e["direction"])
        if key in seen:
            continue
        seen.add(key)
        uniq.append(e)
    return uniq, sorted({m for m in mentions if m})


# --- fetching ---------------------------------------------------------------

def fetch_issues() -> list[dict]:
    return json.loads(gh(["issue", "list", "-R", SLUG, "--state", "all",
                          "--limit", "1000", "--json", ISSUE_FIELDS]))


def fetch_milestones() -> list[dict]:
    return json.loads(gh(["api", f"repos/{SLUG}/milestones?state=all&per_page=100"]))


def fetch_board() -> dict[int, dict]:
    """issue number -> {track, kind, status} from the roadmap board."""
    try:
        raw = gh(["project", "item-list", str(PROJECT_NUMBER), "--owner", PROJECT_OWNER,
                  "--format", "json", "--limit", "1000"])
    except GhError as exc:
        warn(f"board not read ({exc}); track/kind/status will be blank")
        return {}
    board: dict[int, dict] = {}
    for item in json.loads(raw).get("items", []):
        content = item.get("content") or {}
        if content.get("type") != "Issue" or content.get("number") is None:
            continue
        board[int(content["number"])] = {
            "track": item.get("track") or "",
            "kind": item.get("kind") or "",
            "status": item.get("status") or "",
        }
    return board


def stamp(issues: list[dict]) -> str:
    """The data's own freshness stamp: the newest issue edit.

    Not the wall clock - the page has to be byte-identical when nothing on
    GitHub changed, so the timestamp it shows is a fact about the data. That
    also makes a stale page obvious at a glance. SOURCE_DATE_EPOCH pins it for
    reproducible builds.
    """
    override = os.environ.get("SOURCE_DATE_EPOCH")
    if override and override.isdigit():
        return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(int(override)))
    newest = max((i.get("updatedAt") or "" for i in issues), default="")
    return newest or "unknown"


def build_snapshot() -> dict:
    issues = fetch_issues()
    milestones = fetch_milestones()
    board = fetch_board()

    nodes = []
    for raw in sorted(issues, key=lambda i: i["number"]):
        num = raw["number"]
        annotations, mentions = parse_annotations(raw.get("body") or "")
        row = board.get(num, {})
        nodes.append({
            "number": num,
            "title": raw.get("title") or "",
            "state": raw.get("state") or "OPEN",
            "url": raw.get("url") or "",
            "labels": sorted({lbl.get("name", "") for lbl in raw.get("labels") or []} - {""}),
            "milestone": (raw.get("milestone") or {}).get("title") or "",
            "updatedAt": raw.get("updatedAt") or "",
            "closedAt": raw.get("closedAt") or "",
            "track": row.get("track", ""),
            "kind": row.get("kind", ""),
            "status": row.get("status", ""),
            "blockedBy": sorted(n["number"] for n in (raw.get("blockedBy") or {}).get("nodes", [])),
            "blocking": sorted(n["number"] for n in (raw.get("blocking") or {}).get("nodes", [])),
            "annotations": sorted(annotations, key=lambda a: (a["target"], a["direction"])),
            "mentions": [m for m in mentions if m != num],
            "body": raw.get("body") or "",
        })

    snapshot = {
        "generated": stamp(issues),
        "repo": SLUG,
        "issueCount": len(nodes),
        "milestones": [{"number": m["number"], "title": m["title"], "state": m["state"],
                        "createdAt": m.get("created_at", ""), "closedAt": m.get("closed_at") or ""}
                       for m in sorted(milestones, key=lambda m: m["number"])],
        "labels": sorted({lbl for n in nodes for lbl in n["labels"]}),
        "tracks": sorted({n["track"] for n in nodes if n["track"]}),
        "statuses": sorted({n["status"] for n in nodes if n["status"]}),
        "issues": nodes,
    }
    snapshot["warnings"] = analyze(snapshot)
    return snapshot


# --- analysis ---------------------------------------------------------------

def edges_of(snapshot: dict, only_open: bool = False) -> list[tuple[int, int]]:
    """Dependency edges as (blocker, blocked) pairs."""
    known = {n["number"]: n for n in snapshot["issues"]}
    pairs: set[tuple[int, int]] = set()
    for node in snapshot["issues"]:
        if only_open and node["state"] != "OPEN":
            continue
        for other in node["blockedBy"]:
            if other not in known:
                continue
            if only_open and known[other]["state"] != "OPEN":
                continue
            pairs.add((other, node["number"]))
    return sorted(pairs)


def find_cycles(snapshot: dict) -> list[list[int]]:
    """Every strongly connected component of the relation graph, largest first.

    A component of more than one node (or a node blocking itself) is a cycle:
    the issues inside it cannot be ordered, they have to be unblocked together.
    Tarjan's algorithm, iterative so a deep graph cannot blow the Python stack.
    """
    successors: dict[int, set[int]] = {n["number"]: set() for n in snapshot["issues"]}
    for blocker, blocked in edges_of(snapshot):
        successors.setdefault(blocker, set()).add(blocked)
    for node in snapshot["issues"]:
        for other in node["blocking"]:
            if other in successors:
                successors[node["number"]].add(other)
    for n in successors:
        successors[n] = {m for m in successors[n] if m != n}  # self-loops handled below

    index: dict[int, int] = {}
    low: dict[int, int] = {}
    on_stack: set[int] = set()
    stack: list[int] = []
    counter = 0
    components: list[list[int]] = []

    for root in sorted(successors):
        if root in index:
            continue
        work: list[tuple[int, list[int]]] = [(root, sorted(successors[root]))]
        index[root] = low[root] = counter
        counter += 1
        stack.append(root)
        on_stack.add(root)
        while work:
            node, rest = work[-1]
            if rest:
                nxt = rest.pop(0)
                if nxt not in index:
                    index[nxt] = low[nxt] = counter
                    counter += 1
                    stack.append(nxt)
                    on_stack.add(nxt)
                    work.append((nxt, sorted(successors[nxt])))
                elif nxt in on_stack:
                    low[node] = min(low[node], index[nxt])
                continue
            work.pop()
            if low[node] == index[node]:
                component = []
                while True:
                    w = stack.pop()
                    on_stack.discard(w)
                    component.append(w)
                    if w == node:
                        break
                if len(component) > 1:
                    components.append(sorted(component))
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[node])
    for node in snapshot["issues"]:
        if node["number"] in node["blocking"]:
            components.append([node["number"]])
    return sorted(components, key=lambda c: (-len(c), c))


def annotation_edges(snapshot: dict) -> list[tuple[dict, dict]]:
    """Every (issue, annotation) the convention lines state, to a known issue."""
    known = {n["number"] for n in snapshot["issues"]}
    return [(node, a) for node in snapshot["issues"] for a in node["annotations"]
            if a["target"] in known]


def stated_edges(snapshot: dict) -> dict[int, set[int]]:
    """blocker -> {blocked}, from every annotation, in both directions."""
    stated: dict[int, set[int]] = {n["number"]: set() for n in snapshot["issues"]}
    for node, a in annotation_edges(snapshot):
        blocker, blocked = (a["target"], node["number"]) if a["direction"] == "blocked" \
            else (node["number"], a["target"])
        stated.setdefault(blocker, set()).add(blocked)
    return stated


def contradiction_pairs(snapshot: dict) -> set[frozenset[int]]:
    """The {a, b} pairs the prose states in both directions."""
    stated = stated_edges(snapshot)
    return {frozenset((a, b)) for a, targets in stated.items() for b in targets
            if a < b and a in stated.get(b, ())}


def contradictions(snapshot: dict) -> list[str]:
    """Pairs the prose states in both directions, which no relation can hold.

    `#100` says it waits on `#261` (the role list) and `#261` says it waits on
    `#100` (the content it unlocks). A native relation is one edge, so both at
    once is a cycle - GitHub rejects it. These are not drift, they are a question
    for whoever wrote them, so they are listed apart from it.
    """
    return [f"#{a} and #{b} each say they wait for the other -"
            f" one of the two arrows is wrong; set it with tools/dev.py dep"
            for pair in sorted(contradiction_pairs(snapshot), key=sorted)
            for a, b in (sorted(pair),)]


def prose_drift(snapshot: dict) -> list[str]:
    """Prose naming a dependency the native relations do not carry.

    The relations are the edges; the `Depends on` / `Gated by` lines stay as
    human-readable annotation. This reports the two disagreeing, so the backfill
    stays complete. A pair stated in *both* directions is left out: no relation
    can hold it, so it is a contradiction (`contradictions()`), not a missing
    edge, and failing a check on it forever would only train people to ignore it.
    """
    clashes = contradiction_pairs(snapshot)
    drift: list[str] = []
    for node, a in annotation_edges(snapshot):
        have = node["blockedBy"] if a["direction"] == "blocked" else node["blocking"]
        if a["target"] in have:
            continue
        blocker, blocked = (a["target"], node["number"]) if a["direction"] == "blocked" \
            else (node["number"], a["target"])
        if frozenset((blocker, blocked)) in clashes:
            continue
        side = "blockedBy" if a["direction"] == "blocked" else "blocking"
        drift.append(f"#{node['number']} prose says '{a['verb']} #{a['target']}'"
                     f" but {side} does not list it")
    return drift


def planned_relations(snapshot: dict) -> tuple[dict[int, set[int]], list[str]]:
    """The relations the prose states, as blocker -> {blocked}, plus complaints.

    A pair stated in both directions is a contradiction and is left out: GitHub
    makes the relation symmetric, so writing both would be a cycle nobody meant.
    The caller is told about each one rather than having it silently dropped.
    """
    stated = stated_edges(snapshot)
    out_of_repo = [f"#{node['number']} names #{a['target']}, which is not an issue here"
                   for node in snapshot["issues"] for a in node["annotations"]
                   if a["target"] not in {n["number"] for n in snapshot["issues"]}]
    wanted = {k: {b for b in v if k not in stated.get(b, ())}
              for k, v in stated.items() if v}
    return {k: v for k, v in wanted.items() if v}, contradictions(snapshot)


def drop_cyclic_edges(plan: list[tuple[int, list[int]]], existing: dict[int, set[int]]):
    """Take the cycle-closing edges out of a backfill plan, and say which.

    Keeps the plan deterministic and reviewable: an edge that would make one
    issue wait on something that already waits on it is reported instead of
    written, because the page can draw a cycle and GitHub's sidebar cannot.
    """
    reach: dict[int, set[int]] = {}

    def reachable(start: int) -> set[int]:
        if start in reach:
            return reach[start]
        reach[start] = set()
        for b in existing.get(start, ()):
            reach[start] |= reachable(b) | {b}
        return reach[start]

    kept: list[tuple[int, list[int]]] = []
    cyclic: list[str] = []
    for num, add in plan:
        keep = []
        for b in add:
            if num in reachable(b):
                cyclic.append(f"#{num} blocked by #{b} closes a cycle"
                              f" (#{b} already depends on #{num}) - not written")
                continue
            keep.append(b)
        if keep:
            kept.append((num, keep))
            # the accepted edges become part of the graph, so the next issue's
            # cycle check sees them
            existing.setdefault(num, set()).update(keep)
            for b in keep:
                existing.setdefault(b, set()).add(num)
    return kept, cyclic


def analyze(snapshot: dict) -> list[str]:
    """Warnings about a hand-maintained backlog: cycles, dangling edges, drift."""
    problems: list[str] = []
    known = {n["number"] for n in snapshot["issues"]}
    state = {n["number"]: n["state"] for n in snapshot["issues"]}

    dangling = set()
    for node in snapshot["issues"]:
        for other in node["blockedBy"] + node["blocking"]:
            if other not in known:
                dangling.add(f"#{node['number']} -> #{other} (not an issue in this repository)")
            elif other == node["number"]:
                dangling.add(f"#{node['number']} -> itself")
    problems += sorted(dangling)

    for cycle in find_cycles(snapshot):
        shown = ", ".join(f"#{n}" for n in cycle[:12]) + (f", ... (+{len(cycle) - 12} more)" if len(cycle) > 12 else "")
        problems.append(f"dependency cycle: {len(cycle)} issue(s) block each other - {shown}")

    problems += sorted({f"#{blocked} waits on closed #{blocker}"
                        for blocker, blocked in edges_of(snapshot)
                        if state.get(blocked) == "OPEN" and state.get(blocker) == "CLOSED"})
    problems += prose_drift(snapshot)
    problems += contradictions(snapshot)
    return problems


# --- the page ---------------------------------------------------------------

def render_page(snapshot: dict) -> str:
    template = TEMPLATE.read_text(encoding="utf-8")
    payload = json.dumps(snapshot, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    payload = payload.replace("</", "<\\/")  # `</script>` inside a JSON string would close the tag
    # The whole `/*__ROADMAP_DATA__*/null` expression, so nothing is left behind.
    marker = "/*__ROADMAP_DATA__*/null"
    if marker not in template:
        die(f"{TEMPLATE} has no {marker} placeholder")
    return template.replace(marker, payload)


# --- commands ---------------------------------------------------------------

def load_snapshot(prefer_snapshot: bool = False) -> dict:
    if prefer_snapshot and SNAPSHOT.exists():
        return json.loads(SNAPSHOT.read_text(encoding="utf-8"))
    return build_snapshot()


def cmd_generate(args) -> int:
    offline = args.offline
    if not offline:
        try:
            snapshot = build_snapshot()
        except GhError as exc:
            warn(f"could not read GitHub ({exc})")
            if SNAPSHOT.exists():
                warn(f"falling back to the snapshot from the last run ({SNAPSHOT.relative_to(REPO)})")
                offline = True
            else:
                die("no snapshot from an earlier run to fall back on; run this with gh available")
    if offline:
        if not SNAPSHOT.exists():
            die(f"no snapshot at {SNAPSHOT.relative_to(REPO)}."
                " It is generated output and not committed, so run"
                " `python tools/dev.py roadmap` once with `gh` available first")
        snapshot = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
        log(f"snapshot: {snapshot['issueCount']} issues, data as of {snapshot['generated']}")

    ROADMAP_DIR.mkdir(parents=True, exist_ok=True)
    if not args.no_snapshot:
        SNAPSHOT.write_text(json.dumps(snapshot, indent=1, sort_keys=True, ensure_ascii=False) + "\n",
                            encoding="utf-8", newline="\n")
        log(f"wrote {SNAPSHOT.relative_to(REPO)}")

    page = render_page(snapshot)
    if PAGE.exists() and PAGE.read_text(encoding="utf-8") == page and not args.force:
        log(f"{PAGE.relative_to(REPO)} is already up to date")
    else:
        PAGE.write_text(page, encoding="utf-8", newline="\n")
        log(f"wrote {PAGE.relative_to(REPO)} ({len(page) // 1024} KiB)")

    log(f"{snapshot['issueCount']} issues, {len(edges_of(snapshot))} edges,"
        f" {len(find_cycles(snapshot))} cycles, {len(snapshot['warnings'])} warnings")
    for line in snapshot["warnings"]:
        warn(line)
    return 0


def cmd_check(args) -> int:
    """Prose/relation drift, contradictions and cycles; nonzero if drift or a cycle.

    Contradictions are printed but do not fail the check: a pair stated in both
    directions cannot be a relation at all, so the backlog always carries them
    until someone decides which way the arrow points. Drift - one edge stated and
    simply not set - is a real failure, and a cycle is worse.
    """
    snapshot = load_snapshot(prefer_snapshot=True)
    drift = prose_drift(snapshot)
    clashes = contradictions(snapshot)
    cycles = find_cycles(snapshot)
    print(f"prose/relation drift: {len(drift)}")
    for line in drift:
        print(f"  {line}")
    print(f"contradictions in the prose (need a decision, not a relation): {len(clashes)}")
    for line in clashes:
        print(f"  {line}")
    print(f"dependency cycles: {len(cycles)}")
    for cycle in cycles:
        print(f"  {len(cycle)} issue(s) block each other: " + " ".join(f"#{n}" for n in cycle))
    return 1 if cycles or (drift and not args.allow_drift) else 0


def cmd_print_edges(args) -> int:
    snapshot = load_snapshot(prefer_snapshot=args.snapshot)
    titles = {n["number"]: n["title"] for n in snapshot["issues"]}
    for blocker, blocked in edges_of(snapshot):
        print(f"#{blocked} <- #{blocker}  {titles.get(blocked, '')}")
    for line in snapshot["warnings"]:
        print(f"warning: {line}")
    return 0


def cmd_backfill(args) -> int:
    """Set the relations the issue bodies state, through `gh issue edit`.

    Only the convention lines count (`**Depends on:** #96`, `## Builds on /
    depends on` sections), because those are the dependencies someone wrote down
    on purpose. Every edge it creates is printed, so the whole change is
    reviewable as a list before and after it lands, and the pairs the prose
    contradicts each other on are listed and left alone rather than turned into a
    cycle nobody intended.
    """
    snapshot = load_snapshot(prefer_snapshot=args.snapshot)
    wanted, complaints = planned_relations(snapshot)
    existing = {n["number"]: set(n["blockedBy"]) for n in snapshot["issues"]}

    # Only the blocked side is written: GitHub makes the relation symmetric, so
    # `gh issue edit N --add-blocked-by M` also records M as blocking N.
    plan: list[tuple[int, list[int]]] = []
    for node in snapshot["issues"]:
        num = node["number"]
        blockers = sorted(b for b, targets in wanted.items() if num in targets)
        add = [b for b in blockers if b not in existing[num]]
        if add:
            plan.append((num, add))

    # An edge that closes a cycle is dropped rather than written. A dependency
    # graph over a hand-maintained backlog does contain cycles; they are worth
    # knowing about, and they are not worth baking into GitHub's own sidebar.
    plan, cyclic = drop_cyclic_edges(plan, existing)
    if complaints:
        log(f"{len(complaints)} thing(s) the prose does not settle:")
        for line in complaints:
            log(f"  ! {line}")
    if cyclic:
        log(f"{len(cyclic)} edge(s) would close a cycle and are left for a human:")
        for line in cyclic:
            log(f"  ~ {line}")
    if not plan:
        log("nothing to backfill: every stated dependency is already a relation")
        return 0
    log(f"{len(plan)} issues carry {sum(len(b) for _, b in plan)} edges the relations do not")
    for num, add in plan:
        log(f"  #{num}: blocked by {add}")
    if args.dry_run:
        log("dry run; pass --apply to write these relations")
        return 0

    for num, add in plan:
        flags = ["--add-blocked-by", ",".join(str(n) for n in add)]
        proc = gh_run(["issue", "edit", "-R", SLUG, str(num)] + flags, capture=False)
        if proc.returncode != 0:
            warn(f"#{num} failed; rerun to finish the rest")
    log("done; refresh the page with `python tools/dev.py roadmap`")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="tools/roadmap.py",
        description="generate docs/roadmap/roadmap.html: the dependency view over the issues")
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("generate", help="refresh from GitHub and regenerate the page")
    p.add_argument("--offline", action="store_true", help="re-render from the snapshot of the last run, no network")
    p.add_argument("--no-snapshot", action="store_true", help="do not rewrite docs/roadmap/roadmap.json")
    p.add_argument("--force", action="store_true", help="rewrite the page even when unchanged")
    p.set_defaults(func=cmd_generate)

    p = sub.add_parser("check", help="report prose/relation drift and dependency cycles")
    p.add_argument("--allow-drift", action="store_true", help="only fail on cycles")
    p.set_defaults(func=cmd_check)

    p = sub.add_parser("print-edges", help="print every dependency edge and the warnings")
    p.add_argument("--snapshot", action="store_true", help="read the snapshot of the last run instead of GitHub")
    p.set_defaults(func=cmd_print_edges)

    p = sub.add_parser("backfill", help="set the relations the issue bodies state")
    p.add_argument("--snapshot", action="store_true", help="read the snapshot of the last run instead of GitHub")
    p.add_argument("--dry-run", action="store_true", help="only report the edges that would be created")
    p.add_argument("--apply", action="store_true", help="write them (implied when --dry-run is absent)")
    p.set_defaults(func=cmd_backfill)

    args = parser.parse_args(argv if argv is not None else sys.argv[1:])
    if not getattr(args, "command", None):
        args = parser.parse_args((argv or sys.argv[1:]) + ["generate"])
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())