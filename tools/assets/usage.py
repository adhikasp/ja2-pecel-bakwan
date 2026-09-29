#!/usr/bin/env python3
"""Per-screen asset usage map, from the image-load log the game writes during a tour.

    python tools/ja2ctl.py run tests/spike/asset_usage_tour.lua --isolated --out ~/.ja2-assets/tour
    python tools/assets/usage.py ~/.ja2-assets/tour/image_usage.jsonl     # -> ~/.ja2-assets/usage.json

Each log line is {"screen": "MAP_SCREEN", "file": "interface/mapinv.sti", "frame": 123} (ja2.recordImageUsage,
hooked into CreateImage in src/sgp/HImage.cc). The output maps every image (lower-case, Data-relative, forward
slashes) to the screens that loaded it and how often; manifest.py uses it to order the work.
An image the game caches is only loaded, and so counted, on the first screen that needs it.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ja2assets import DEFAULT_WORK_DIR  # noqa: E402


def normalise(name: str) -> str:
    return name.replace("\\", "/").lower().lstrip("./")


def build_usage(lines) -> dict:
    files: dict[str, dict] = {}
    screens: dict[str, set] = {}
    for line in lines:
        line = line.strip()
        if not line:
            continue
        rec = json.loads(line)
        f = normalise(rec["file"])
        s = rec.get("screen", "UNKNOWN_SCREEN")
        e = files.setdefault(f, {"screens": [], "loads": 0})
        e["loads"] += 1
        if s not in e["screens"]:
            e["screens"].append(s)
        screens.setdefault(s, set()).add(f)
    return {
        "files": dict(sorted(files.items())),
        "screens": {s: sorted(v) for s, v in sorted(screens.items())},
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+", type=Path, help="image_usage.jsonl file(s) from tours")
    ap.add_argument("--out", type=Path, default=DEFAULT_WORK_DIR / "usage.json")
    args = ap.parse_args(argv)
    lines = []
    for log in args.logs:
        lines += log.read_text(encoding="utf-8").splitlines()
    usage = build_usage(lines)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(usage, indent=1), encoding="utf-8")
    print(f"{len(usage['files'])} images on {len(usage['screens'])} screens -> {args.out}")
    for s, fs in usage["screens"].items():
        print(f"  {s}: {len(fs)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
