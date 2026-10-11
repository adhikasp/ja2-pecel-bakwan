#!/usr/bin/env python3
"""Unit tests for the dev.py worktree helper (tools/worktrees.py).

The parsing and the safety rules are pure functions over `git worktree list`
output, so most of what matters - which rows may be deleted - is asserted here
without touching a repository. The integration test at the bottom then builds
a throwaway repository with a fake remote and prunes it for real, because the
only way to know the rules hold together is to run them against git.

    python -m unittest discover -s tools/tests
"""

import io
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import worktrees  # noqa: E402
from gittest import git_fails, isolate_git, make_repo, quiet, require_git, run  # noqa: E402

# A worktree list shaped like the ones this tool actually meets: the main
# checkout, a space in a path (every agent worktree on Windows), detached
# heads, a locked tree and a record whose directory is gone.
PORCELAIN = """worktree C:/repo
HEAD 1111111111111111111111111111111111111111
branch refs/heads/master

worktree C:/Users/someone/agent tree/one
HEAD 2222222222222222222222222222222222222222
branch refs/heads/feat/one

worktree C:/repo/.claude/worktrees/detached
HEAD 3333333333333333333333333333333333333333
detached

worktree C:/repo/.claude/worktrees/locked
HEAD 4444444444444444444444444444444444444444
branch refs/heads/claude/locked
locked

worktree C:/repo/.claude/worktrees/gone
HEAD 5555555555555555555555555555555555555555
branch refs/heads/claude/gone
prunable gitdir file points to non-existent location
"""


def captured_say():
    """A `say` callback that collects the plan, one line per entry."""
    buffer = io.StringIO()
    return buffer, lambda line="": buffer.write(line + "\n")


class ParseWorktrees(unittest.TestCase):
    def setUp(self):
        self.rows = worktrees.parse_worktrees(PORCELAIN)

    def test_every_record_becomes_one_row(self):
        self.assertEqual(5, len(self.rows))

    def test_paths_with_spaces_survive(self):
        self.assertEqual("C:/Users/someone/agent tree/one", self.rows[1].path)

    def test_branch_is_the_short_name(self):
        self.assertEqual("feat/one", self.rows[1].branch)

    def test_detached_has_no_branch(self):
        self.assertIsNone(self.rows[2].branch)
        self.assertEqual("3333333", self.rows[2].head[:7])

    def test_locked_and_prunable_flags(self):
        self.assertTrue(self.rows[3].locked)
        self.assertTrue(self.rows[4].missing_dir)

    def test_empty_input(self):
        self.assertEqual([], worktrees.parse_worktrees(""))


class Formatting(unittest.TestCase):
    def test_sizes(self):
        self.assertEqual("-", worktrees.format_size(None))
        self.assertEqual("0 B", worktrees.format_size(0))
        self.assertEqual("1.0 kB", worktrees.format_size(1024))
        self.assertEqual("2.8 GB", worktrees.format_size(int(2.75 * (1 << 30))))
        self.assertEqual("1.0 TB", worktrees.format_size(1 << 40))

    def test_ages(self):
        self.assertEqual("-", worktrees.format_age(None))
        self.assertEqual("now", worktrees.format_age(5))
        self.assertEqual("30m", worktrees.format_age(1800))
        self.assertEqual("5h", worktrees.format_age(5 * 3600))
        self.assertEqual("3d", worktrees.format_age(3 * 86400))
        self.assertEqual("1.0y", worktrees.format_age(366 * 86400))

    def test_paths_are_shortened_relative_to_the_checkout(self):
        self.assertEqual(".claude/worktrees/detached",
                         worktrees.shorten("C:/repo/.claude/worktrees/detached", "C:/repo", "C:/Users/x"))
        self.assertEqual("~/wt/one",
                         worktrees.shorten("C:/Users/x/wt/one", "C:/repo", "C:/Users/x"))

    def test_a_root_that_realpath_expands_still_slices_the_path_correctly(self):
        # `realpath` resolves a relative-looking root against the current
        # directory, so on Linux the resolved root is much longer than the text
        # the path is written with. Slicing with the resolved length empties
        # the path, which is what the Linux CI run caught.
        self.assertEqual(".claude/worktrees/x",
                         worktrees.shorten("repo/.claude/worktrees/x", "./repo", "/nowhere"))
        self.assertEqual("wt/one", worktrees.shorten("/a/wt/one", "/a", "/nowhere"))
        self.assertEqual("/elsewhere/wt", worktrees.shorten("/elsewhere/wt", "/a", "/nowhere"))

    def test_table_puts_the_prunable_rows_first_and_flags_them(self):
        rows = worktrees.parse_worktrees(PORCELAIN)
        rows[0].main = True
        for row in rows:
            row.exists = row.path != "C:/repo/.claude/worktrees/gone"
            row.dirty = False
        rows[2].merged = True       # detached, already on master
        rows[4].merged = True       # directory gone: only the record is left
        rows[1].pr = {"number": 42, "state": "OPEN"}
        text = worktrees.render(rows, "C:/repo", "C:/Users/x")
        lines = text.splitlines()
        self.assertTrue(lines[0].startswith("SIZE"))
        self.assertIn("merged", lines[1])
        self.assertIn("#42 open", text)
        self.assertIn("locked", text)
        self.assertIn("(detached)", text)
        # Prunable rows sort to the top: the detached merged tree is listed
        # before the open-PR one.
        self.assertLess(text.index(".claude/worktrees/detached"), text.index("feat/one"))


class PruneSafety(unittest.TestCase):
    """The whole point of the command: what it refuses to delete."""

    def row(self, **kwargs):
        base = dict(path="C:/wt/x", head="a" * 40, branch="feat/x", exists=True,
                    merged=True, dirty=False)
        base.update(kwargs)
        return worktrees.Row(**base)

    def test_merged_and_clean_is_prunable(self):
        self.assertIsNone(worktrees.prune_blocker(self.row()))

    def test_the_main_checkout_is_never_prunable(self):
        self.assertEqual("the main checkout",
                         worktrees.prune_blocker(self.row(main=True, path="C:/repo")))

    def test_the_worktree_you_stand_in_is_never_prunable(self):
        self.assertEqual("this worktree", worktrees.prune_blocker(self.row(here=True)))

    def test_unmerged_work_is_never_prunable(self):
        self.assertEqual("unmerged work", worktrees.prune_blocker(self.row(merged=False)))

    def test_a_merged_pull_request_counts_as_merged(self):
        # Squash merges: the branch head is not an ancestor of master, so the
        # pull request is the only evidence the work got in.
        row = self.row(merged=False, pr={"number": 7, "state": "MERGED"})
        self.assertTrue(row.in_master)
        self.assertIsNone(worktrees.prune_blocker(row))

    def test_an_open_pull_request_is_never_prunable(self):
        row = self.row(pr={"number": 8, "state": "OPEN"})
        self.assertEqual("PR #8 is open", worktrees.prune_blocker(row))

    def test_uncommitted_changes_are_never_prunable(self):
        self.assertEqual("uncommitted changes", worktrees.prune_blocker(self.row(dirty=True)))

    def test_a_running_build_is_never_prunable(self):
        self.assertEqual("a build is running in it", worktrees.prune_blocker(self.row(busy=True)))

    def test_a_locked_worktree_is_never_prunable(self):
        self.assertEqual("locked", worktrees.prune_blocker(self.row(locked=True)))

    def test_a_vanished_directory_only_leaves_its_record(self):
        self.assertIsNone(worktrees.prune_blocker(self.row(exists=False, merged=False, dirty=False)))

    def test_a_deleted_branch_with_unmerged_commits_is_kept(self):
        row = self.row(branch_exists=False, merged=False)
        self.assertEqual("unmerged work", worktrees.prune_blocker(row))
        self.assertIn("branch gone", worktrees.state_of(row))

    def test_json_carries_the_reason(self):
        row = self.row(merged=False)
        payload = row.as_json()
        self.assertFalse(payload["prunable"])
        self.assertEqual("unmerged work", payload["blocked_because"])


class BranchDeletion(unittest.TestCase):
    """`git branch -d` judges against HEAD, which is never master when an agent
    prunes from its own worktree; the merge test is made against master here."""

    def setUp(self):
        if not shutil.which("git"):
            self.skipTest("git not on PATH")
        isolate_git(self.addCleanup)
        self.tmp = Path(tempfile.mkdtemp(prefix="ja2-branch-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.repo, self.origin = make_repo(self.tmp)
        run("checkout", "-q", "-b", "feat", cwd=self.repo)
        (self.repo / "feature.txt").write_text("work\n", encoding="utf-8")
        run("add", "feature.txt", cwd=self.repo)
        run("commit", "-q", "-m", "feature", cwd=self.repo)
        run("checkout", "-q", "master", cwd=self.repo)
        run("merge", "-q", "--ff-only", "feat", cwd=self.repo)
        run("push", "-q", "origin", "master", cwd=self.repo)  # master has to be master *on the remote*
        run("fetch", "-q", "origin", cwd=self.repo)
        # Stand where an agent stands: not on master.
        run("checkout", "-q", "--detach", "HEAD~1", cwd=self.repo)
        self.row = worktrees.Row(path=str(self.repo), head=run("rev-parse", "HEAD", cwd=self.repo).strip(),
                                 branch="feat", exists=True, merged=True)

    def branches(self):
        return run("branch", "--list", "feat", cwd=self.repo).strip()

    def test_a_branch_merged_into_master_goes(self):
        said = io.StringIO()
        worktrees.delete_branch(self.repo, self.row, "refs/remotes/origin/master", said.write)
        self.assertEqual("", self.branches())
        self.assertIn("deleted branch feat", said.getvalue())

    def test_a_branch_that_is_not_in_master_stays(self):
        self.row.branch = "nothing-here"
        self.row.pr = None
        said = io.StringIO()
        worktrees.delete_branch(self.repo, self.row, "refs/remotes/origin/master", said.write)
        self.assertIn("kept branch nothing-here", said.getvalue())
        self.assertIn("not in", said.getvalue())

    def test_without_a_base_nothing_is_deleted(self):
        said = io.StringIO()
        worktrees.delete_branch(self.repo, self.row, None, said.write)
        self.assertEqual("feat", self.branches().split()[0].strip("* "))
        self.assertIn("kept branch feat", said.getvalue())


class BuildLockDetection(unittest.TestCase):
    def test_a_fresh_lock_file_means_a_build_is_running(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "_bin").mkdir()
            (root / "_bin" / worktrees.BUILD_LOCK).write_text("{}", encoding="utf-8")
            self.assertTrue(worktrees.is_build_running(str(root)))

    def test_no_lock_file_means_no_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertFalse(worktrees.is_build_running(tmp))

    def test_a_stale_lock_file_is_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "build").mkdir()
            lock = root / "build" / worktrees.BUILD_LOCK
            lock.write_text("{}", encoding="utf-8")
            os.utime(lock, (0, 0))
            self.assertFalse(worktrees.is_build_running(str(root)))


class SizeCache(unittest.TestCase):
    def test_sizes_are_measured_and_reused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.txt").write_bytes(b"x" * 1000)
            (root / "sub").mkdir()
            (root / "sub" / "b.txt").write_bytes(b"y" * 24)
            self.assertEqual(1024, worktrees.dir_size(str(root)))
            cache_file = root / "state" / "worktree-sizes.json"
            self.assertEqual({}, worktrees.load_size_cache(cache_file))
            worktrees.save_size_cache(cache_file, {str(root): {"bytes": 1024, "at": 0}})
            self.assertEqual(1024, worktrees.load_size_cache(cache_file)[str(root)]["bytes"])

    def test_measure_handles_several_trees(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ("a", "b"):
                (root / name).mkdir()
                (root / name / "f").write_bytes(b"z" * 10)
            sizes = worktrees.measure([str(root / "a"), str(root / "b")])
            self.assertEqual({10, 10}, set(sizes.values()))


class AgainstRealGit(unittest.TestCase):
    """The rules end to end: a throwaway repository, a fake remote, a real prune."""

    def setUp(self):
        require_git(self)
        isolate_git(self.addCleanup)
        self.tmp = Path(tempfile.mkdtemp(prefix="ja2-wt-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.repo, self.origin = make_repo(self.tmp)
        self.state = self.tmp / "state"
        self.rows = {}

    def add_worktree(self, name, *, commit=None, branch=None, detached=False):
        path = self.tmp / name
        if detached:
            run("worktree", "add", "--detach", str(path), "HEAD", cwd=self.repo)
        else:
            run("worktree", "add", "-b", branch or name, str(path), "HEAD", cwd=self.repo)
            # Content per branch: a worktree branched after a merge would
            # otherwise have nothing to commit.
            (path / "file.txt").write_text(f"{name}\n", encoding="utf-8")
            run("add", "file.txt", cwd=path)
            run("commit", "-m", commit or f"work on {name}", cwd=path)
        self.rows[name] = str(path)
        return path

    def merge_to_master(self, name):
        """What a merged branch looks like: its head is an ancestor of master."""
        run("merge", "--ff-only", name, cwd=self.repo)
        run("push", "origin", "master", cwd=self.repo)
        run("fetch", "origin", cwd=self.repo)

    def at(self, *parts):
        """A worktree path in the form git prints it, whatever the platform."""
        return worktrees.normalize(self.tmp.joinpath(*parts))

    def collect(self, **kwargs):
        rows, base = worktrees.collect(self.repo, here=self.repo, fetch=False, prs={}, **kwargs)
        return rows

    def test_lists_every_worktree_with_a_state(self):
        self.add_worktree("done")
        rows = self.collect()
        self.assertEqual(2, len(rows))
        self.assertTrue(rows[0].main)
        states = {r.name: worktrees.state_of(r) for r in rows}
        self.assertEqual("main", states["master"])
        self.assertEqual("unmerged", states["done"])

    def test_prunes_a_merged_branch_and_deletes_its_directory(self):
        done = self.add_worktree("done")
        (done / "_bin").mkdir()
        (done / "_bin" / "objects.bin").write_bytes(b"0" * 4096)   # the build dir it is here for
        self.merge_to_master("done")
        rows = self.collect()
        self.assertIsNone(worktrees.prune_blocker(next(r for r in rows if r.branch == "done")))
        quiet(worktrees.prune, self.repo, rows, yes=True)
        self.assertFalse(done.exists())
        self.assertEqual([], [r for r in worktrees.parse_worktrees(
            run("worktree", "list", "--porcelain", cwd=self.repo)) if r.branch == "done"])

    def test_keeps_a_worktree_whose_branch_is_not_merged(self):
        live = self.add_worktree("live")
        rows = self.collect()
        self.assertEqual("unmerged work", worktrees.prune_blocker(next(r for r in rows if r.branch == "live")))
        quiet(worktrees.prune, self.repo, rows, yes=True)
        self.assertTrue(live.exists())

    def test_keeps_a_worktree_with_uncommitted_changes(self):
        dirty = self.add_worktree("dirty")
        self.merge_to_master("dirty")
        (dirty / "scratch.txt").write_text("not committed\n", encoding="utf-8")
        rows = self.collect()
        doomed, kept = worktrees.plan_prune(rows)
        self.assertNotIn(self.at("dirty"), [worktrees.normalize(r.path) for r in doomed])
        self.assertIn(self.at("dirty"), [worktrees.normalize(r.path) for r in kept])
        quiet(worktrees.prune, self.repo, rows, yes=True)
        self.assertTrue(dirty.exists())

    def test_the_prune_plan_says_which_rows_the_dirty_check_turned_away(self):
        # A listing that says "37 prunable" and a plan that says "36" with no
        # explanation reads as a bug in the tool.
        dirty = self.add_worktree("dirty")
        self.merge_to_master("dirty")
        (dirty / "scratch.txt").write_text("not committed\n", encoding="utf-8")
        self.add_worktree("clean")
        self.merge_to_master("clean")
        said, say = captured_say()
        quiet(worktrees.prune, self.repo, self.collect(), dry_run=True, say=say, log=say)
        text = said.getvalue()
        self.assertIn("About to remove 1 worktree(s)", text)
        kept = [line for line in text.splitlines() if "(kept)" in line]
        self.assertEqual(1, len(kept))
        self.assertIn("uncommitted changes", kept[0])
        self.assertNotIn("clean", kept[0])

    def test_keeps_a_locked_worktree_even_when_merged(self):
        locked = self.add_worktree("locked")
        self.merge_to_master("locked")
        run("worktree", "lock", str(locked), cwd=self.repo)
        rows = self.collect()
        self.assertEqual("locked", worktrees.prune_blocker(next(r for r in rows if r.branch == "locked")))
        quiet(worktrees.prune, self.repo, rows, yes=True)
        self.assertTrue(locked.exists())

    def test_a_detached_tree_whose_commit_is_not_on_master_survives(self):
        # A detached HEAD has no branch to look a pull request up by, so it is
        # unmerged work unless its commit is already an ancestor of master: the
        # row is listed, kept, and says why.
        self.add_worktree("source")                     # commits nothing onto master
        run("worktree", "add", "--detach", str(self.tmp / "stray"), "source", cwd=self.repo)
        rows = self.collect()
        row = next(r for r in rows if worktrees.normalize(r.path) == self.at("stray"))
        self.assertIsNone(row.branch)
        self.assertFalse(row.in_master)
        self.assertEqual("unmerged work", worktrees.prune_blocker(row))
        quiet(worktrees.prune, self.repo, rows, yes=True)
        self.assertTrue((self.tmp / "stray").exists())

    def test_dry_run_changes_nothing(self):
        done = self.add_worktree("done")
        self.merge_to_master("done")
        rows = self.collect()
        quiet(worktrees.prune, self.repo, rows, dry_run=True)
        self.assertTrue(done.exists())
        self.assertEqual(2, len(worktrees.parse_worktrees(
            run("worktree", "list", "--porcelain", cwd=self.repo))))

    def test_json_output_is_machine_readable(self):
        self.add_worktree("live")
        code = quiet(worktrees.main, ["--json", "--no-fetch", "--no-pr"], repo=self.repo, here=self.repo,
                              state_dir=self.state)
        self.assertEqual(0, code)

    def test_squash_merged_pull_request_makes_a_row_prunable(self):
        # The shape this repository actually produces: the branch head is not an
        # ancestor of master, so only the pull request can say the work is in.
        merged = self.add_worktree("merged")
        run("push", "origin", f"HEAD:refs/heads/merged-{merged.name}", cwd=merged)
        rows, _ = worktrees.collect(self.repo, here=self.repo, fetch=False,
                                    prs={"merged": {"number": 5, "state": "MERGED"}})
        row = next(r for r in rows if worktrees.normalize(r.path) == worktrees.normalize(merged))
        self.assertFalse(row.merged, "ancestry alone does not see a squash merge")
        self.assertTrue(row.in_master)
        self.assertIsNone(worktrees.prune_blocker(row))

    def test_a_merged_branch_is_deleted_from_a_worktree_that_is_not_master(self):
        # `git branch -d` judges against HEAD, and an agent prunes from its own
        # worktree, whose branch does not contain anyone else's commits. The
        # merge test has to be made against master, or no branch is ever
        # collected and the output blames git for a refusal it never made.
        done = self.add_worktree("done")
        agent = self.add_worktree("agent")       # our own worktree, created first
        self.add_worktree("other")               # unmerged, so it survives the prune
        self.merge_to_master("done")             # in master now, but NOT in agent's HEAD
        rows, _ = worktrees.collect(self.repo, here=agent, fetch=False, prs={})
        said, say = captured_say()
        quiet(worktrees.prune, agent, rows, yes=True, say=say, log=say)
        self.assertEqual("", run("branch", "--list", "done", cwd=self.repo).strip())
        self.assertEqual("agent", run("rev-parse", "--abbrev-ref", "HEAD", cwd=agent).strip())
        self.assertIn("deleted branch done", said.getvalue())

    def test_a_branch_merged_by_pull_request_is_deleted_even_without_ancestry(self):
        # After a squash merge the branch head is not an ancestor of master, so
        # the merged pull request is the only evidence - and enough, and the
        # message has to say that is why.
        orphan = self.add_worktree("orphan")
        rows = [row for row in self.collect()
                if worktrees.normalize(row.path) == self.at("orphan")]
        self.assertEqual(1, len(rows))
        rows[0].pr = {"number": 9, "state": "MERGED"}
        self.assertIsNone(worktrees.prune_blocker(rows[0]))
        said, say = captured_say()
        quiet(worktrees.prune, self.repo, rows, yes=True, say=say, log=say)
        self.assertEqual("", run("branch", "--list", "orphan", cwd=self.repo).strip())
        self.assertFalse(orphan.exists())
        self.assertIn("deleted branch orphan (merged as PR #9)", said.getvalue())

    def test_a_branch_another_worktree_holds_is_kept(self):
        # git refuses this one on its own, and the message has to say so: the
        # branch is alive precisely because some other checkout is using it.
        self.add_worktree("held")
        self.merge_to_master("held")
        holder = self.tmp / "holder"
        run("worktree", "add", "--force", str(holder), "held", cwd=self.repo)
        rows, _ = worktrees.collect(self.repo, here=holder, fetch=False, prs={})
        said, say = captured_say()
        quiet(worktrees.prune, self.repo, rows, yes=True, say=say, log=say)
        self.assertIn("held", run("branch", "--list", "held", cwd=self.repo))
        self.assertIn("kept branch held", said.getvalue())
        self.assertIn("worktree", said.getvalue())


if __name__ == "__main__":
    unittest.main()
