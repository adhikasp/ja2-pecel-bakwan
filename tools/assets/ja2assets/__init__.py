"""Asset tooling for the native modern game plan (docs/plan/native-modern-game.md, "Asset methodology").

Everything here reads the player's own game data and writes to a work directory outside git
(default ~/.ja2-assets). Original or derived art is never committed; tests use synthetic images.
"""
from __future__ import annotations

import json
import os
from pathlib import Path

def _home() -> Path:
    try:
        return Path.home()
    except RuntimeError:  # no HOME/USERPROFILE (some CI or test environments)
        return Path.cwd()


DEFAULT_WORK_DIR = Path(os.environ.get("JA2_ASSETS_DIR", _home() / ".ja2-assets"))


def find_data_dir() -> Path | None:
    """The Data directory of the game, from $JA2_DATA_DIR or game_dir in the user's ja2.json."""
    env = os.environ.get("JA2_DATA_DIR")
    if env:
        return Path(env)
    candidates = []
    if os.name == "nt" and os.environ.get("APPDATA"):
        candidates.append(Path(os.environ["APPDATA"]) / "JA2" / "ja2.json")
    candidates.append(_home() / ".ja2" / "ja2.json")
    for c in candidates:
        try:
            cfg = json.loads(c.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        game_dir = cfg.get("game_dir")
        if game_dir:
            for sub in ("Data", "data", "DATA"):
                if (Path(game_dir) / sub).is_dir():
                    return Path(game_dir) / sub
    return None
