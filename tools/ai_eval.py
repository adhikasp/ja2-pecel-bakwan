#!/usr/bin/env python3
"""Run the AI evaluation matrix: staged AI-vs-AI battles, one isolated process per
matchup, reported as a table (issue #59, docs/plan/ai-evaluation.md).

    python tools/ai_eval.py                        # the default matchups
    python tools/ai_eval.py --matchup elite        # one of them (repeatable)
    python tools/ai_eval.py --seeds 1,2,3          # a distribution instead of one sample
    python tools/ai_eval.py --json eval.json       # save the raw reports
    python tools/ai_eval.py --baseline eval.json   # deltas against a saved run

Each matchup runs tests/e2e/battle_ai_eval.lua --isolated with the matchup spec in
--arg; the scenario plays the fight out AI-only and prints one "AIEVAL {...}" line,
which this tool collects. The table is the evaluation: verdict, losses inflicted vs
taken, time to disengage and whether the objective was held. Compare at the same seed:
a different seed is a different battle, not a delta.
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
JA2CTL = HERE / "ja2ctl.py"
SCRIPT = REPO / "tests" / "e2e" / "battle_ai_eval.lua"

# The default matrix: one AI-vs-AI question each (docs/plan/ai-evaluation.md). The
# battlefield is the same E11 layout every time; the matchup scales it.
MATCHUPS = [
    ("even",    {"militia": 10, "class": "green", "enemies": 10, "enemy_class": "administrator"}),
    ("numbers", {"militia": 20, "class": "green", "enemies": 10, "enemy_class": "administrator"}),
    ("army",    {"militia": 10, "class": "green", "enemies": 10, "enemy_class": "army"}),
    ("elite",   {"militia": 10, "class": "green", "enemies": 10, "enemy_class": "elite"}),
]

AIEVAL = re.compile(r"^\s*AIEVAL (\{.*\})\s*$")


def run_matchup(name: str, spec: dict, seed, res: str, timeout: int, rounds: int):
    """One isolated battle; returns the AIEVAL line's dict, or raises RuntimeError."""
    arg = ",".join(f"{k}={v}" for k, v in spec.items())
    arg += f",rounds={rounds}"
    cmd = [sys.executable, str(JA2CTL), "run", str(SCRIPT), "--isolated", "--res", res,
           "--timeout", str(timeout)]
    if seed is not None:
        cmd += ["--seed", str(seed)]
    with tempfile.TemporaryDirectory(prefix="ja2-ai-eval-") as out:
        cmd += ["--out", out, "--arg", arg]
        proc = subprocess.run(cmd, capture_output=True, text=True)
    for line in proc.stdout.splitlines():
        m = AIEVAL.match(line)
        if m:
            return json.loads(m.group(1))
    tail = "\n".join(proc.stdout.splitlines()[-10:])
    raise RuntimeError(f"{name} (seed {seed}) failed with exit code {proc.returncode}:\n{tail}")


def secs(value) -> str:
    return "never" if value is None or value < 0 else f"{value:.1f}s"


def report_row(name: str, seed, data: dict) -> dict:
    """The numbers the table shows, flattened. `data` is the AIEVAL line: the verdict on
    the AI force, the matchup and the report."""
    report = data["report"]
    p, e = report["player"], report["enemy"]
    return {
        "name": name,
        "seed": seed,
        "verdict": data.get("verdict", report["outcome"]),
        "outcome": report["outcome"],
        "endedBy": report["endedBy"],
        "rounds": report["rounds"],
        "seconds": report["seconds"],
        "player_alive": p["alive"],
        "player_dead": p["dead"],
        "enemy_alive": e["alive"],
        "enemy_dead": e["dead"],
        "damage_player": p["damageDealt"],
        "damage_enemy": e["damageDealt"],
        "shots_player": p["shots"],
        "shots_enemy": e["shots"],
        "first_break": report["firstBreakSeconds"],
        "disengage": report["disengageSeconds"],
        "objective": (report.get("objective") or {}).get("held"),
    }


def print_table(rows, baseline=None):
    base = {(r["name"], r["seed"]): r for r in (baseline or [])}

    def delta(row, key, fmt=str, unit=""):
        old = base.get((row["name"], row["seed"]))
        value = fmt(row[key]) + unit
        if not old:
            return value
        before, after = old[key], row[key]
        diff = (after or 0) - (before or 0)
        if isinstance(diff, float):
            diff = round(diff, 1)
        return f"{value} ({diff:+})"

    header = (f"{'matchup':<9} {'seed':>4}  {'verdict':<9} {'rnd':>4}  {'time':>7}  "
              f"{'militia':>8} {'enemy':>7}  {'damage P/E':>13}  {'shots P/E':>10}  "
              f"{'break':>7}  {'obj':>3}")
    print(header)
    print("-" * len(header))
    for row in rows:
        militia = delta(row, "player_alive") + "/" + delta(row, "player_dead")
        enemy = delta(row, "enemy_alive") + "/" + delta(row, "enemy_dead")
        damage = delta(row, "damage_player") + "/" + delta(row, "damage_enemy")
        shots = delta(row, "shots_player") + "/" + delta(row, "shots_enemy")
        obj = {True: "yes", False: "no", None: "-"}[row["objective"]]
        print(f"{row['name']:<9} {row['seed'] if row['seed'] is not None else '-':>4}  "
              f"{row['verdict']:<9} {delta(row, 'rounds'):>4}  {delta(row, 'seconds', lambda v: f'{v:.1f}', 's'):>7}  "
              f"{militia:>8} {enemy:>7}  {damage:>13}  {shots:>10}  "
              f"{secs(row['first_break']):>7}  {obj:>3}")
    print()
    print("verdict = the militia alone (the report's outcome is about the two sides, and the")
    print("player's side includes the one observer merc: he never fires or is hit, so militia")
    print("left = alive - 1). 'break' is the first HOPELESS verdict, 'obj' whether the player")
    print("held the enemy line's centre when combat ended.")
    if base:
        changed = [r for r in rows if (r["name"], r["seed"]) in base
                   and base[(r["name"], r["seed"])]["verdict"] != r["verdict"]]
        if changed:
            print()
            print("verdict changes against the baseline:")
            for r in changed:
                print(f"  {r['name']} @{r['seed']}: "
                      f"{base[(r['name'], r['seed'])]['verdict']} -> {r['verdict']}")


def main():
    parser = argparse.ArgumentParser(
        prog="tools/ai_eval.py",
        description="run the AI evaluation matrix and report the battle metrics")
    parser.add_argument("--matchup", action="append", default=None,
                        help="matchup to run (repeatable; default: all)")
    parser.add_argument("--seeds", default="1",
                        help="comma-separated RNG seeds to run each matchup at (default 1)")
    parser.add_argument("--rounds", type=int, default=12,
                        help="round bound per matchup: a fight still running at the bound is "
                             "reported unresolved (default 12)")
    parser.add_argument("--res", default="1920x1080", help="resolution (the native HUD needs >= 1280x720)")
    parser.add_argument("--timeout", type=int, default=900, help="wall-clock seconds per battle")
    parser.add_argument("--json", dest="json_path", default=None,
                        help="write the raw reports to this file")
    parser.add_argument("--baseline", default=None,
                        help="a --json file from an earlier run: print the deltas against it")
    opts = parser.parse_args()

    wanted = opts.matchup or [name for name, _ in MATCHUPS]
    known = {name for name, _ in MATCHUPS}
    unknown = [name for name in wanted if name not in known]
    if unknown:
        sys.exit(f"ai_eval: unknown matchup(s) {', '.join(unknown)}; have {', '.join(sorted(known))}")
    seeds = [int(s) for s in opts.seeds.split(",") if s.strip()] or [None]

    rows = []
    saved = []
    for name in wanted:
        spec = dict(next(s for n, s in MATCHUPS if n == name))
        for seed in seeds:
            print(f"ai_eval: {name} @ seed {seed}...", file=sys.stderr, flush=True)
            try:
                data = run_matchup(name, spec, seed, opts.res, opts.timeout, opts.rounds)
            except RuntimeError as error:
                sys.exit(f"ai_eval: {error}")
            saved.append({"name": name, "seed": seed, "spec": spec,
                          "verdict": data["verdict"], "report": data["report"]})
            rows.append(report_row(name, seed, data))

    baseline = None
    if opts.baseline:
        try:
            baseline = [report_row(m["name"], m.get("seed"), m)
                        for m in json.loads(Path(opts.baseline).read_text(encoding="utf-8"))["matchups"]]
        except (OSError, KeyError, ValueError) as error:
            sys.exit(f"ai_eval: could not read the baseline {opts.baseline}: {error}")

    print_table(rows, baseline)

    if opts.json_path:
        Path(opts.json_path).write_text(
            json.dumps({"matchups": saved}, indent=2) + "\n", encoding="utf-8")
        print(f"\nai_eval: reports written to {opts.json_path}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
