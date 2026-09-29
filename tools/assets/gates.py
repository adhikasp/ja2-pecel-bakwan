#!/usr/bin/env python3
"""Runs the quality gates on recreated art and writes a comparison sheet.

    python tools/assets/gates.py ORIGINALS CANDIDATES [--sheet sheet.html] [--json gates.json] [--budget-kb 256]

ORIGINALS is a directory from extract.py (e.g. ~/.ja2-assets/extracted); CANDIDATES has the same layout with the
HD frames (e.g. ~/.ja2-assets/hd). For every candidate frame_NNN.png with an original, the per-image gates run
(exact scale multiple, silhouette, colour drift, size budget); animations (several frames) also get the frame
stability gate. The sheet shows original and candidate side by side, pixelated, with the gate results, for
review. Exit code 1 if any gate fails.
"""
from __future__ import annotations

import argparse
import html
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ja2assets import gates, png  # noqa: E402


def check_dir(orig_dir: Path, cand_dir: Path, budget: int | None) -> list[dict]:
    rows = []
    origs, cands = [], []
    for cand in sorted(cand_dir.glob("frame_*.png")):
        orig = orig_dir / cand.name
        if not orig.is_file():
            continue
        o, c = png.read(orig), png.read(cand)
        origs.append(o)
        cands.append(c)
        results = gates.run_all(o, c, cand.stat().st_size, budget)
        rows.append({"original": str(orig), "candidate": str(cand), "results": [r.as_dict() for r in results]})
    if len(origs) > 1 and all(r["results"][0]["passed"] for r in rows):
        stab = gates.frame_stability(origs, cands)
        rows.append({"original": str(orig_dir), "candidate": str(cand_dir), "results": [stab.as_dict()]})
    return rows


def sheet(rows: list[dict], out: Path) -> None:
    def rel(p: str) -> str:
        return Path(os.path.relpath(p, out.parent)).as_posix()

    cells = []
    for r in rows:
        ok = all(g["passed"] for g in r["results"])
        gates_html = "".join(
            f"<li class='{'ok' if g['passed'] else 'bad'}'>{g['gate']}: {g['value']} (limit {g['limit']}) {html.escape(g['detail'])}</li>"
            for g in r["results"])
        imgs = ""
        if r["original"].endswith(".png"):
            imgs = f"<img src='{rel(r['original'])}' class='o'><img src='{rel(r['candidate'])}' class='c'>"
        cells.append(f"<div class='cell {'ok' if ok else 'bad'}'><div class='p'>{html.escape(r['candidate'])}</div>"
                     f"<div class='imgs'>{imgs}</div><ul>{gates_html}</ul></div>")
    out.write_text(f"""<!doctype html><meta charset="utf-8"><title>Comparison sheet</title>
<style>body{{background:#101418;color:#e6e9ec;font:13px system-ui;margin:20px}}
.cell{{border:1px solid #2c3640;border-radius:6px;padding:10px;margin:10px 0}} .cell.bad{{border-color:#c8553d}}
.imgs{{display:flex;gap:16px;align-items:flex-start;background:repeating-conic-gradient(#222 0 25%,#2a2a2a 0 50%) 0 0/16px 16px;padding:8px}}
img{{image-rendering:pixelated}} img.o{{zoom:4}} .p{{color:#8d99a6}} li.ok{{color:#8fe3a0}} li.bad{{color:#ff8a7a}}</style>
<h1>Comparison sheet</h1><p>Original (4x, nearest) | candidate (1x)</p>{''.join(cells)}""", encoding="utf-8")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("originals", type=Path)
    ap.add_argument("candidates", type=Path)
    ap.add_argument("--sheet", type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--budget-kb", type=int, default=0)
    args = ap.parse_args(argv)
    budget = args.budget_kb * 1024 if args.budget_kb else None
    rows = []
    for meta in sorted(args.candidates.rglob("frame_000.png")):
        d = meta.parent
        rows += check_dir(args.originals / d.relative_to(args.candidates), d, budget)
    failed = sum(1 for r in rows for g in r["results"] if not g["passed"])
    if args.json:
        args.json.write_text(json.dumps(rows, indent=1), encoding="utf-8")
    if args.sheet:
        sheet(rows, args.sheet)
    print(f"{len(rows)} checks, {failed} failed gates")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
