#!/usr/bin/env python3
"""Unit tests for the dev.py screenshot publisher (tools/shots.py).

The branch names and the links are pure functions, but the interesting part -
does the published branch end up an orphan holding only images, and does a
second run with the same images do nothing - needs git. So these tests build a
throwaway repository with a local "remote" (a bare repo on disk): no network,
no credentials, and every assertion is about a real pushed commit.

    python -m unittest discover -s tools/tests
"""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import shots  # noqa: E402
from gittest import git_fails, isolate_git, make_repo, quiet, require_git, run  # noqa: E402

PNG = (b"\x89PNG\r\n\x1a\n" + b"\x00" * 64)  # a file with the right magic, not a real image


class BranchNames(unittest.TestCase):
    def test_a_slashed_branch_keeps_its_shape(self):
        # README.md links already read pr-screenshots/feat/123-ui/map.png
        self.assertEqual("feat/123-ui", shots.safe_branch_dir("feat/123-ui"))
        self.assertEqual("docs-readme-screenshots-refresh",
                         shots.safe_branch_dir("docs-readme-screenshots-refresh"))

    def test_characters_a_filesystem_rejects_become_dashes(self):
        self.assertEqual("feat-a-b-c", shots.safe_branch_dir('feat:a"b|c'))
        self.assertEqual("a-b-c-d-e", shots.safe_branch_dir("a<b>c?d*e"))

    def test_nothing_can_escape_the_worktree(self):
        self.assertEqual("etc/passwd", shots.safe_branch_dir("../../etc/passwd"))
        self.assertEqual("passwd", shots.safe_branch_dir("..\\..\\passwd"))

    def test_a_name_that_is_all_junk_is_refused(self):
        for junk in ("", "   ", "..", "/", "///"):
            with self.assertRaises(shots.ShotError, msg=junk):
                shots.safe_branch_dir(junk)


class Links(unittest.TestCase):
    def test_the_link_is_the_raw_url_of_the_branch(self):
        self.assertEqual(
            "https://raw.githubusercontent.com/adhikasp/ja2-pecel-bakwan/"
            "pr-screenshots/feat/1-ui/map_1920x1080.png",
            shots.image_url("feat/1-ui", "map_1920x1080.png"))

    def test_another_fork_is_honoured(self):
        self.assertIn("someone/ja2/pr-screenshots/x/a.png",
                      shots.image_url("x", "a.png", owner="someone", repo="ja2"))

    def test_spaces_in_a_file_name_are_escaped(self):
        self.assertIn("my%20shot.png", shots.image_url("x", "my shot.png"))

    def test_markdown_carries_a_readable_caption(self):
        self.assertEqual(
            "![map 1920x1080](https://raw.githubusercontent.com/adhikasp/ja2-pecel-bakwan/"
            "pr-screenshots/x/map_1920x1080.png)",
            shots.markdown_link("x", "map_1920x1080.png"))


class RemoteNames(unittest.TestCase):
    def test_ssh_and_https_and_trailing_git(self):
        for url in ("git@github.com:adhikasp/ja2-pecel-bakwan.git",
                    "https://github.com/adhikasp/ja2-pecel-bakwan.git",
                    "https://github.com/adhikasp/ja2-pecel-bakwan"):
            self.assertEqual(("adhikasp", "ja2-pecel-bakwan"), shots.slug_from_remote(url), url)

    def test_something_unrecognisable_falls_back_to_our_fork(self):
        self.assertEqual((shots.DEFAULT_OWNER, shots.DEFAULT_REPO), shots.slug_from_remote("origin"))
        # A local path keeps its last two segments, which is what a file:// or
        # plain-path remote needs.
        self.assertEqual(("local", "path"), shots.slug_from_remote("/local/path"))


class CollectingImages(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="ja2-shots-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def write(self, name, data=PNG):
        path = self.tmp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_a_missing_file_is_an_error_before_anything_moves(self):
        with self.assertRaises(shots.ShotError):
            shots.collect_images([str(self.tmp / "nope.png")], self.tmp)

    def test_a_file_that_is_not_an_image_is_refused(self):
        path = self.write("notes.txt", b"hello")
        with self.assertRaises(shots.ShotError):
            shots.collect_images([str(path)], self.tmp)

    def test_relative_paths_resolve_against_the_current_directory(self):
        path = self.write("a.png")
        self.assertEqual([path.resolve()], shots.collect_images(["a.png"], self.tmp))

    def test_the_same_image_twice_is_published_once(self):
        path = self.write("a.png")
        self.assertEqual([path.resolve()], shots.collect_images([str(path), str(path)], self.tmp))


class Publishing(unittest.TestCase):
    """The whole flow, against a real repository and a real (local) remote."""

    def setUp(self):
        require_git(self)
        isolate_git(self.addCleanup)
        self.tmp = Path(tempfile.mkdtemp(prefix="ja2-shots-repo-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.work = self.tmp / "state" / "pr-screenshots"
        self.repo, self.origin = make_repo(self.tmp)
        self.image = self.tmp / "map_1920x1080.png"
        self.image.write_bytes(PNG)

    def published(self):
        """What the remote's pr-screenshots branch holds, as (path -> bytes)."""
        listing = run("ls-tree", "-r", "--name-only", "pr-screenshots", cwd=self.origin)
        files = {}
        for name in listing.split():
            files[name] = subprocess.run(["git", "show", f"pr-screenshots:{name}"],
                                         cwd=str(self.origin), capture_output=True).stdout
        return files

    def publish(self, *images, branch="issue-170", **kwargs):
        return quiet(shots.publish, self.repo, self.work, branch,
                     [Path(p) for p in images], **kwargs)

    def test_creates_an_orphan_branch_holding_only_the_images(self):
        links = self.publish(self.image)
        self.assertEqual(1, len(links))
        self.assertIn("pr-screenshots/issue-170/map_1920x1080.png", links[0])
        # An orphan branch shares no history with master, and holds the images
        # and nothing else: the two properties the old dance kept getting wrong.
        # `git merge-base` exits non-zero precisely when there is no common
        # ancestor, which here is the point.
        self.assertTrue(git_fails("merge-base", "master", "pr-screenshots", cwd=self.repo),
                        "the screenshots branch must share no history with master")
        self.assertEqual({"issue-170/map_1920x1080.png": PNG}, self.published())

    def test_the_image_is_on_the_remote_with_its_bytes(self):
        self.publish(self.image)
        self.assertEqual({"issue-170/map_1920x1080.png": PNG}, self.published())

    def test_publishing_twice_changes_nothing(self):
        self.publish(self.image)
        first = run("rev-parse", "pr-screenshots", cwd=self.origin).strip()
        self.publish(self.image)
        self.assertEqual(first, run("rev-parse", "pr-screenshots", cwd=self.origin).strip())

    def test_a_second_branch_adds_to_the_same_branch(self):
        self.publish(self.image)
        other = self.tmp / "menu.png"
        other.write_bytes(PNG + b"2")
        self.publish(other, branch="issue-171")
        self.assertEqual({"issue-170/map_1920x1080.png", "issue-171/menu.png"},
                         set(self.published()))

    def test_a_new_image_on_a_branch_is_a_second_commit(self):
        self.publish(self.image)
        second = self.tmp / "menu.png"
        second.write_bytes(PNG + b"2")
        self.publish(second)
        self.assertEqual("2", run("rev-list", "--count", "pr-screenshots", cwd=self.origin).strip())
        self.assertEqual({"issue-170/map_1920x1080.png", "issue-170/menu.png"},
                         set(self.published()))

    def test_the_worktree_is_reused_rather_than_created_again(self):
        self.publish(self.image)
        self.publish(self.image, branch="issue-172")
        listed = run("worktree", "list", "--porcelain", cwd=self.repo)
        self.assertEqual(2, len([l for l in listed.splitlines() if l.startswith("worktree ")]))

    def test_the_worktree_is_detached_so_a_checked_out_branch_cannot_block_it(self):
        # pr-screenshots checked out somewhere else is the normal state of this
        # machine; publishing must still work, and must not touch that tree.
        other = self.tmp / "other-shots"
        run("worktree", "add", "--detach", str(other), "HEAD", cwd=self.repo)
        run("checkout", "--orphan", "pr-screenshots", cwd=other)
        # `checkout --orphan` leaves the checkout's files staged, which is how
        # the hand-typed dance ended up publishing a copy of the source tree
        # alongside the images; empty it before committing.
        run("rm", "-rfq", "--ignore-unmatch", ".", cwd=other)
        run("clean", "-qfdx", cwd=other)
        (other / "keep.txt").write_text("x\n", encoding="utf-8")
        run("add", "keep.txt", cwd=other)
        run("commit", "-m", "existing images", cwd=other)
        run("push", "origin", "pr-screenshots", cwd=other)
        self.publish(self.image)
        self.assertEqual({"keep.txt", "issue-170/map_1920x1080.png"}, set(self.published()))
        self.assertEqual("x\n", (other / "keep.txt").read_text(encoding="utf-8"))

    def test_dry_run_publishes_nothing_but_still_reports_the_links(self):
        links = self.publish(self.image, dry_run=True)
        self.assertEqual(1, len(links))
        self.assertIn("pr-screenshots/issue-170/map_1920x1080.png", links[0])
        self.assertTrue(git_fails("rev-parse", "--verify", "pr-screenshots", cwd=self.origin),
                        "the branch must not exist on the remote after a dry run")

    def test_two_images_with_one_name_are_refused_before_any_worktree_is_made(self):
        # Copying them would leave a single file and two identical links - and
        # refusing only once the worktree exists leaks a checkout every call.
        first = self.tmp / "one" / "map.png"
        second = self.tmp / "two" / "map.png"
        for path, payload in ((first, PNG), (second, PNG + b"2")):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        with self.assertRaises(shots.ShotError):
            self.publish(first, second)
        self.assertFalse(self.work.exists(), "a refused run must not leave a checkout behind")

    def test_a_directory_in_the_way_is_refused_rather_than_deleted(self):
        self.work.mkdir(parents=True)
        (self.work / "precious.txt").write_text("do not delete me", encoding="utf-8")
        with self.assertRaises(shots.ShotError):
            self.publish(self.image)
        self.assertTrue((self.work / "precious.txt").exists())

    def test_the_commit_message_names_the_branch(self):
        self.publish(self.image)
        self.assertEqual("Screenshots for issue-170",
                         run("log", "-1", "--format=%s", "pr-screenshots", cwd=self.origin).strip())


if __name__ == "__main__":
    unittest.main()
