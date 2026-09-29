#!/usr/bin/env python3
"""Run the same e2e script twice with the same seed; the screenshots it takes
must be byte-for-byte identical. Guards the virtual clock, virtual audio and
seeded RNG against anything that lets wall-clock time or entropy leak in.

    python tests/e2e/check_determinism.py [script.lua]
"""

import filecmp
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
JA2CTL = HERE.parent.parent / "tools" / "ja2ctl.py"


def run(script: Path, out: Path) -> None:
    code = subprocess.call([sys.executable, str(JA2CTL), "run", str(script), "--isolated",
                            "--seed", "7", "--out", str(out)])
    if code != 0:
        sys.exit(f"{script.name} failed with exit code {code}")


def main() -> int:
    script = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "tactical_move_merc.lua"
    with tempfile.TemporaryDirectory(prefix="ja2-determinism-") as tmp:
        a, b = Path(tmp) / "a", Path(tmp) / "b"
        run(script, a)
        run(script, b)
        shots = sorted(p.name for p in a.glob("*.png"))
        if not shots:
            print(f"{script.name} took no screenshots; nothing to compare", file=sys.stderr)
            return 2
        different = [s for s in shots if not (b / s).exists() or not filecmp.cmp(a / s, b / s, shallow=False)]
        if different:
            print(f"not deterministic: {', '.join(different)} differ between two runs", file=sys.stderr)
            return 1
        print(f"deterministic: {len(shots)} screenshot(s) identical across two runs ({', '.join(shots)})")
        return 0


if __name__ == "__main__":
    sys.exit(main())
