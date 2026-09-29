"""The proposed method for every asset (docs/plan/native-modern-game.md, "Asset methodology").

Rules are ordered; the first match wins. They look at the Data-relative path only, so the manifest can be rebuilt
from any copy of the game. Change the rules here, not by hand in assets/manifest.json.
"""
from __future__ import annotations

import re

METHODS = {
    "design-new": "Designed new from the design system (vector/procedural UI chrome, icons, cursors, markers)",
    "replace": "Replaced by an open-licence equivalent (fonts)",
    "regenerate": "Rendered from game data at HD (radar maps, strategic map)",
    "upscale": "Pixel-art-aware upscale + cleanup (faces, items, world tiles, animations, load screens, video)",
    "repaint": "Hand repaint / new art for the most-seen content (big merc portraits)",
}

# (regex on the lower-case path, category, method, note)
RULES: list[tuple[str, str, str, str]] = [
    (r"^fonts/|(^|/)[^/]*(font|fnt)[^/]*\.sti$", "font", "replace", "bitmap font -> TTF/OTF"),
    (r"^cursors/", "cursor", "design-new", ""),
    (r"^radarmaps/", "radar-map", "regenerate", "overhead map rendered from the sector"),
    (r"^interface/b_map\.pcx$", "strategic-map", "regenerate", "map rendered from sector data"),
    (r"^faces/bigfaces/", "portrait", "repaint", "large merc portraits: the most-seen content"),
    (r"^faces/", "portrait", "upscale", "eye/mouth frames must stay aligned"),
    (r"^interface/(credit faces|smfaces|mdguns|mditems|mdp\d?items|[^/]*faces?)\.sti$", "content-in-ui", "upscale", "content art inside the UI archive"),
    (r"^interface/(mine|sam)\.pcx$", "content-in-ui", "upscale", "picture"),
    (r"^interface/inventory_figure|^interface/inventory_normal_male", "content-in-ui", "upscale", "inventory body figure"),
    (r"^bigitems/", "item", "upscale", ""),
    (r"^loadscreens/", "load-screen", "upscale", ""),
    (r"^anims/", "animation", "upscale", "palette-swap body parts via recolour masks; frame stability gate"),
    (r"^tilecache/", "effect", "upscale", "explosions, gas, smoke"),
    (r"^tilesets/", "world-tile", "upscale", "tile seam gate"),
    (r"^laptop/(flower_\d|[^/]*ad_\d+|mcgillicuttys|enrico_[wy]|explosion|mortuary)\.sti$", "web-content", "upscale", "picture on a web page"),
    (r"^laptop/", "laptop-ui", "design-new", "rebuilt as RML pages"),
    (r"^interface/", "ui-chrome", "design-new", ""),
    (r"^intro/.*\.smk$", "video", "upscale", "optional"),
    (r"^[^/]+\.(sti|pcx)$", "editor", "design-new", "map editor art (editor: keep legacy or rebuild, Phase 7)"),
]
_COMPILED = [(re.compile(p), c, m, n) for p, c, m, n in RULES]


def classify(path: str) -> dict:
    p = path.replace("\\", "/").lower()
    for rx, category, method, note in _COMPILED:
        if rx.search(p):
            return {"category": category, "method": method, "note": note}
    return {"category": "other", "method": "upscale", "note": "unclassified: review"}
