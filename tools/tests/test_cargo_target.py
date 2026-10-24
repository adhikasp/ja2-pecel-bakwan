#!/usr/bin/env python3
"""One cargo target dir for every checkout, or a full Rust build per worktree.

The compiler cache does not cover the Rust workspace: sccache only caches
`rlib` crates, and 45 crates here build as a staticlib or binaries, which it
rejects with the reason `crate-type`. So a build directory with a `target/` of
its own recompiles all 53 crates - ~46 s on a 12-thread machine, 46 of the ~49 s
a fresh worktree spends. Sharing one cargo target dir is what turns that into
the three workspace crates cargo keys by checkout path.

The env var `dev.py` sets is the whole mechanism, and an unset one is invisible
in a build log (it only costs time), so it is asserted here rather than
described in a comment.

    python -m unittest discover -s tools/tests
"""

import contextlib
import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import dev  # noqa: E402


@contextlib.contextmanager
def env(**overrides):
    """Run the block with these variables set (`None` removes one)."""
    saved = {name: os.environ.get(name) for name in overrides}
    try:
        for name, value in overrides.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
        yield
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


class CargoTargetDir(unittest.TestCase):
    def test_env_carries_the_target_dir(self):
        """The env a build runs in must name the shared dir, absolute."""
        target = dev.cargo_target_dir()
        self.assertIsNotNone(target)
        self.assertTrue(Path(target).is_absolute(), target)
        self.assertEqual(dev.tool_env()[dev.CARGO_TARGET_DIR_ENV], target)

    def test_default_lives_in_the_machine_cache(self):
        """Unset means a machine-wide cache directory, never one inside a checkout."""
        with env(**{dev.CARGO_TARGET_DIR_ENV: None, "XDG_CACHE_HOME": None}):
            target = Path(dev.cargo_target_dir())
            self.assertTrue(target.is_absolute(), target)
            self.assertEqual(target.name, "cargo-target")
            self.assertFalse(str(target).startswith(str(dev.REPO)), target)
            if not dev.IS_WINDOWS:
                self.assertEqual(target, dev.user_home() / ".cache" / "ja2" / "cargo-target")
        if not dev.IS_WINDOWS:
            with env(**{dev.CARGO_TARGET_DIR_ENV: None, "XDG_CACHE_HOME": "/var/cache/x"}):
                self.assertEqual(dev.cargo_target_dir(), "/var/cache/x/ja2/cargo-target")

    def test_explicit_setting_wins(self):
        with env(**{dev.CARGO_TARGET_DIR_ENV: "/srv/ja2-cargo-target"}):
            self.assertEqual(dev.cargo_target_dir(), "/srv/ja2-cargo-target")
            self.assertEqual(dev.tool_env()[dev.CARGO_TARGET_DIR_ENV], "/srv/ja2-cargo-target")

    def test_an_empty_setting_means_one_dir_per_build(self):
        """The opt-out: no shared dir, so no other checkout's artifacts are seen."""
        with env(**{dev.CARGO_TARGET_DIR_ENV: ""}):
            self.assertIsNone(dev.cargo_target_dir())
            self.assertNotIn(dev.CARGO_TARGET_DIR_ENV, dev.tool_env())

    def test_a_changed_target_dir_reconfigures(self):
        """cmake reads the value once, so a build dir configured with another must follow it."""
        import json
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            stamp = {"generator": "Ninja", "venv": dev.venv_python() is not None}
            with env(**{dev.CARGO_TARGET_DIR_ENV: "/srv/one"}):
                stamp["cargo_target"] = dev.cargo_target_dir()
                (build / ".dev-setup.json").write_text(json.dumps(stamp), encoding="utf-8")
                self.assertFalse(dev.stamp_stale(build))
            with env(**{dev.CARGO_TARGET_DIR_ENV: "/srv/two"}):
                self.assertTrue(dev.stamp_stale(build))
            # A build directory configured before this setting existed reconfigures once.
            (build / ".dev-setup.json").write_text(
                json.dumps({k: v for k, v in stamp.items() if k != "cargo_target"}),
                encoding="utf-8")
            self.assertTrue(dev.stamp_stale(build))


if __name__ == "__main__":
    unittest.main()
