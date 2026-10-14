#!/usr/bin/env python3
"""The compiler cache is shared by every checkout on the machine, or it is not.

sccache decides what a cache entry is keyed on, and it reads the directories it
should strip out of those keys from `SCCACHE_BASEDIRS`. sccache 0.16 spelled
that `SCCACHE_BASEDIR` and reads it no longer, silently: setting the old name
still builds, still fills the cache, and silently never produces a hit in a
second checkout. That is a bug nobody sees, so the name is asserted here
against a real sccache rather than in a comment - `test_sccache_reads_...`
starts a server with `dev.py`'s own environment and asks it what it ended up
with.

    python -m unittest discover -s tools/tests
"""

import os
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import dev  # noqa: E402


def have_sccache():
    return dev.tool_which("sccache") is not None


class SccacheBaseDirs(unittest.TestCase):
    def test_env_carries_the_plural_name(self):
        """The env a build runs in must set SCCACHE_BASEDIRS, not the old singular."""
        env = dev.tool_env()
        self.assertIn("SCCACHE_BASEDIRS", env)
        self.assertNotIn("SCCACHE_BASEDIR", env)

    def test_base_dirs_cover_every_checkout_here(self):
        """Every worktree this repo has here goes in, the one under test included.

        They sit at different absolute paths - that is the whole point - so a
        build in any of them has to have its own root stripped to hit the others.
        """
        roots = dev.worktree_roots()
        self.assertEqual(roots[0], dev.REPO)
        self.assertTrue(all(p.is_absolute() for p in roots))
        listed = dev.sccache_basedirs().split(os.pathsep)
        self.assertEqual(listed, [str(p) for p in roots])
        self.assertIn(str(dev.REPO), listed)

    def test_base_dirs_are_what_the_separator_wants(self):
        """sccache splits on ';' on Windows and ':' everywhere else, like os.pathsep."""
        expected = ";" if dev.IS_WINDOWS else ":"
        self.assertEqual(os.pathsep, expected)

    @unittest.skipUnless(dev.IS_WINDOWS, "MSYS2 path spelling is a Windows problem")
    def test_msys_style_roots_become_windows_paths(self):
        """MSYS2's git spells a Windows path `/c/Users/...`; sccache needs `C:/Users/...`.

        Which git answers depends on the shell `dev.py` was started from - MSYS2's
        from its own shell, Git for Windows from cmd - and sccache will not start
        with a base directory that is not absolute, which fails every compile in
        the build rather than quietly missing the cache.
        """
        self.assertEqual(dev.native_root("/c/Workspace/x"), "C:/Workspace/x")
        self.assertEqual(dev.native_root(r"C:\Workspace\x"), "C:/Workspace/x")
        roots = dev.parse_worktree_list(
            "worktree /c/Workspace/one\nHEAD 1\n\nworktree C:/Workspace/two\nHEAD 2\n")
        self.assertEqual([str(p) for p in roots], ["C:\\Workspace\\one", "C:\\Workspace\\two"])
        self.assertTrue(all(p.is_absolute() for p in roots))

    def test_a_root_that_is_not_absolute_is_dropped(self):
        """Better one checkout missing from the list than every compile failing."""
        good = "C:/ok" if dev.IS_WINDOWS else "/ok"
        roots = dev.parse_worktree_list(f"worktree relative/one\nworktree {good}\n")
        self.assertEqual([str(p) for p in roots], [str(Path(good))])
        self.assertTrue(roots[0].is_absolute())

    @unittest.skipUnless(have_sccache(), "sccache is not installed")
    def test_sccache_reads_the_name_we_set(self):
        """The assertion that would have caught the silent rename.

        Ask a server started with our environment what base directories it has.
        It must report the checkouts from `dev.py`, normalized by sccache the way
        `normalize_root` expects. If a future sccache renames the setting again,
        this fails here instead of quietly making every checkout a cold build.
        """
        dev.sync_sccache_server()  # a server older than the current checkout set is replaced
        reported = dev.sccache_reported_basedirs()
        self.assertIsNotNone(reported, "sccache did not report its base directories; "
                                       "is it too old for `--show-stats --stats-format json`?")
        want = {dev.normalize_root(p) for p in dev.worktree_roots()}
        self.assertTrue(want <= reported,
                        f"sccache dropped {sorted(want - reported)} from its base directories; "
                        f"it reported {sorted(reported)}")


if __name__ == "__main__":
    unittest.main()
