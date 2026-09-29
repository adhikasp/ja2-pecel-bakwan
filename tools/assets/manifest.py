#!/usr/bin/env python3
"""Builds the asset manifest and the HTML inventory report.

    python tools/assets/manifest.py                    # assets/manifest.json + ~/.ja2-assets/inventory.html
    python tools/assets/manifest.py --usage ~/.ja2-assets/usage.json --report /tmp/inventory.html

The manifest lists every image and video of the game (names and sizes only, never pixels) with its category,
the proposed method (ja2assets/classify.py), a status and the screens that use it (usage.py). It is the work
list of Phase 9 and the input of the quality gates; it may be committed, the art may not.
"""
from __future__ import annotations

import argparse
import html
import json
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ja2assets import DEFAULT_WORK_DIR, find_data_dir  # noqa: E402
from ja2assets.classify import METHODS, classify  # noqa: E402
from ja2assets.slf import SlfArchive  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
ART_EXTS = (".sti", ".pcx", ".tga", ".smk")


def sti_info(head: bytes) -> dict:
    """Size and frame count from the first bytes of an STI."""
    if len(head) < 64 or head[:4] != b"STCI":
        return {}
    flags, height, width = struct.unpack_from("<IHH", head, 16)
    info = {"size": [width, height], "depth": head[44]}
    if flags & 0x0008:  # indexed
        (frames,) = struct.unpack_from("<H", head, 28)
        info["frames"] = frames if flags & 0x0020 else 1
    else:
        info["frames"] = 1
    return info


def collect(data_dir: Path) -> list[dict]:
    assets: dict[str, dict] = {}
    for slf in sorted(data_dir.glob("*.slf"), key=lambda p: p.name.lower()):
        with SlfArchive(slf) as a:
            for e in a.files():
                if not e.name.lower().endswith(ART_EXTS):
                    continue
                path = a.full_path(e)
                rec = {"path": path, "archive": slf.name, "bytes": e.length}
                if path.lower().endswith(".sti"):
                    a._f.seek(e.offset)
                    rec.update(sti_info(a._f.read(64)))
                assets[path.lower()] = rec
    for f in sorted(data_dir.rglob("*")):
        if f.is_file() and f.suffix.lower() in ART_EXTS:
            path = f.relative_to(data_dir).as_posix()
            rec = {"path": path, "archive": "(loose)", "bytes": f.stat().st_size}
            if f.suffix.lower() == ".sti":
                with open(f, "rb") as fh:
                    rec.update(sti_info(fh.read(64)))
            assets[path.lower()] = rec  # loose files override archives, like the VFS
    return list(assets.values())


def build_manifest(assets: list[dict], usage: dict | None) -> dict:
    used = (usage or {}).get("files", {})
    out = []
    notes: dict[str, str] = {}
    for a in sorted(assets, key=lambda r: r["path"].lower()):
        c = classify(a["path"])
        rec = {"path": a["path"], "archive": a["archive"], "category": c["category"], "method": c["method"]}
        for k in ("frames", "size"):
            if k in a:
                rec[k] = a[k]
        u = used.get(a["path"].lower())
        if u:
            rec["usage"] = u["screens"]
        if c["note"]:
            notes[c["category"]] = c["note"]
        out.append(rec)
    by_method = Counter(r["method"] for r in out)
    by_category = Counter(r["category"] for r in out)
    return {
        "version": 1,
        "about": "Every image/video of JA2 Gold with its proposed method. Names only: no art is stored here. "
                 "Built by tools/assets/manifest.py; edit the rules in tools/assets/ja2assets/classify.py.",
        "methods": METHODS,
        "status": "Every asset is 'todo' unless it has a \"status\" of its own (in-progress, review, done).",
        "categoryNotes": dict(sorted(notes.items())),
        "summary": {"assets": len(out), "byMethod": dict(sorted(by_method.items())),
                    "byCategory": dict(sorted(by_category.items())),
                    "withUsage": sum(1 for r in out if "usage" in r)},
        "assets": out,
    }


def dump_manifest(m: dict) -> str:
    """JSON with one asset per line, so diffs stay readable."""
    head = {k: v for k, v in m.items() if k != "assets"}
    text = json.dumps(head, indent=1)[:-2] + ',\n "assets": [\n'
    text += ",\n".join("  " + json.dumps(a, separators=(", ", ": ")) for a in m["assets"])
    return text + "\n ]\n}\n"


def render_report(m: dict) -> str:
    rows = []
    for a in m["assets"]:
        size = "x".join(map(str, a.get("size", []))) if a.get("size") else ""
        rows.append("<tr data-m='{m}' data-c='{c}'><td>{p}</td><td>{ar}</td><td>{c}</td><td class='m {m}'>{m}</td>"
                    "<td>{f}</td><td>{s}</td><td>{u}</td><td>{n}</td></tr>".format(
                        p=html.escape(a["path"]), ar=html.escape(a["archive"]), c=a["category"], m=a["method"],
                        f=a.get("frames", ""), s=size, u=html.escape(", ".join(a.get("usage", []))),
                        n=html.escape(m["categoryNotes"].get(a["category"], ""))))
    summary = "".join(f"<tr><td class='m {k}'>{k}</td><td>{v}</td><td>{html.escape(METHODS[k])}</td></tr>"
                      for k, v in m["summary"]["byMethod"].items())
    cats = "".join(f"<tr><td>{k}</td><td>{v}</td></tr>" for k, v in m["summary"]["byCategory"].items())
    return f"""<!doctype html><html><head><meta charset="utf-8"><title>JA2 asset inventory</title>
<style>
body{{font:14px/1.4 system-ui,sans-serif;margin:24px;background:#101418;color:#e6e9ec}}
h1{{margin:0 0 4px}} .sub{{color:#8d99a6;margin-bottom:18px}}
table{{border-collapse:collapse}} td,th{{padding:3px 10px;border-bottom:1px solid #2c3640;text-align:left;vertical-align:top}}
th{{color:#8d99a6;font-weight:600;position:sticky;top:0;background:#101418}}
.m{{font-weight:600}} .design-new{{color:#7cc4ff}} .replace{{color:#c9a0ff}} .regenerate{{color:#8fe3a0}}
.upscale{{color:#e8b654}} .repaint{{color:#ff8a7a}}
.grid{{display:flex;gap:40px;margin-bottom:20px}} input,select{{background:#1b2229;color:#e6e9ec;border:1px solid #2c3640;padding:6px}}
</style></head><body>
<h1>JA2 asset inventory</h1>
<div class="sub">{m['summary']['assets']} assets, {m['summary']['withUsage']} seen in the usage tour. Names only; generated by tools/assets/manifest.py.</div>
<div class="grid"><table><tr><th>Method</th><th>Assets</th><th></th></tr>{summary}</table>
<table><tr><th>Category</th><th>Assets</th></tr>{cats}</table></div>
<p><input id="q" placeholder="filter by path, screen..." size="40"> <select id="fm"><option value="">all methods</option>
{''.join(f'<option>{k}</option>' for k in METHODS)}</select> <span id="n"></span></p>
<table id="t"><tr><th>Path</th><th>Archive</th><th>Category</th><th>Method</th><th>Frames</th><th>Size</th><th>Used on</th><th>Note</th></tr>
{''.join(rows)}</table>
<script>
const q=document.getElementById('q'),fm=document.getElementById('fm'),n=document.getElementById('n');
function f(){{let c=0;for(const r of document.querySelectorAll('#t tr[data-m]')){{
const ok=(!fm.value||r.dataset.m==fm.value)&&r.textContent.toLowerCase().includes(q.value.toLowerCase());
r.style.display=ok?'':'none';c+=ok}}n.textContent=c+' shown'}}
q.oninput=f;fm.onchange=f;f();
</script></body></html>"""


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, help="the game's Data directory (default: from ja2.json)")
    ap.add_argument("--usage", type=Path, default=DEFAULT_WORK_DIR / "usage.json")
    ap.add_argument("--manifest", type=Path, default=REPO / "assets" / "manifest.json")
    ap.add_argument("--report", type=Path, default=DEFAULT_WORK_DIR / "inventory.html")
    args = ap.parse_args(argv)
    data_dir = args.data or find_data_dir()
    if not data_dir or not data_dir.is_dir():
        print("error: no game Data directory (use --data or set game_dir in ja2.json)", file=sys.stderr)
        return 2
    usage = json.loads(args.usage.read_text(encoding="utf-8")) if args.usage.is_file() else None
    m = build_manifest(collect(data_dir), usage)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(dump_manifest(m), encoding="utf-8", newline="\n")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(render_report(m), encoding="utf-8")
    s = m["summary"]
    print(f"{s['assets']} assets ({s['withUsage']} with usage) -> {args.manifest}, {args.report}")
    for k, v in s["byMethod"].items():
        print(f"  {k}: {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
