#!/usr/bin/env python3
"""Two ja2ctl sessions side by side: each is driven independently, neither
sees the other's input, and both shut down cleanly.

    python tests/e2e/check_sessions.py
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

JA2CTL = Path(__file__).resolve().parent.parent.parent / "tools" / "ja2ctl.py"


def ja2ctl(session: str, *args: str) -> str:
    result = subprocess.run([sys.executable, str(JA2CTL), "-s", session, "--json", *args],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"ja2ctl -s {session} {' '.join(args)} failed:\n{result.stdout}{result.stderr}")
    return result.stdout


def screen(session: str) -> str:
    return json.loads(ja2ctl(session, "screen"))["result"]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="ja2ctl-sessions-", ignore_cleanup_errors=True) as root:
        os.environ["JA2CTL_HOME"] = root
        try:
            ja2ctl("alpha", "start")
            ja2ctl("beta", "start")
            assert screen("alpha") == screen("beta") == "MAINMENU_SCREEN", "both boot to the main menu"

            ja2ctl("alpha", "click", "New Game")
            ja2ctl("alpha", "wait-idle")
            assert screen("alpha") == "GAME_INIT_OPTIONS_SCREEN", "alpha moved on"
            assert screen("beta") == "MAINMENU_SCREEN", "beta is unaffected by alpha's input"

            ja2ctl("beta", "click", "Preferences")
            ja2ctl("beta", "wait-idle")
            assert screen("beta") == "OPTIONS_SCREEN", "beta moved on"
            assert screen("alpha") == "GAME_INIT_OPTIONS_SCREEN", "alpha is unaffected by beta's input"

            homes = {s: Path(root) / s / "home" for s in ("alpha", "beta")}
            assert all(h.is_dir() for h in homes.values()), "each session has its own home"
        finally:
            subprocess.call([sys.executable, str(JA2CTL), "stop", "--all"])
        remaining = [p for p in Path(root).glob("*/session.json")]
        assert not remaining, f"sessions still registered after stop: {remaining}"
    print("sessions: two independent sessions ran side by side and stopped cleanly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
