"""Shared scaffolding for the tests of the dev.py tooling.

These tests run real git, and two things about that are worth doing once
here rather than in every test:

  * **Isolation.** The developer's own git configuration - a hooks path, a
    signing key, an excludes file, a `safe.directory` list - must not decide
    whether these tests pass, and the identity these commits need must not
    come from a global config either. The variables below cover both the git
    the test starts and the git the code under test starts, because the code
    under test inherits this process's environment.
  * **Speed.** Spawning git costs about a quarter of a second on Windows, so
    the setup is built out of the fewest commands that can work, and the tests
    that only need one branch use one.

`run()` raises with git's own message on failure; a test that cannot build its
fixture should say so loudly rather than assert against nothing.
"""

import contextlib
import io
import os
import shutil
import subprocess

ISOLATED_ENV = {
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_SYSTEM": os.devnull,
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "ja2 dev tooling tests",
    "GIT_AUTHOR_EMAIL": "tests@example.invalid",
    "GIT_COMMITTER_NAME": "ja2 dev tooling tests",
    "GIT_COMMITTER_EMAIL": "tests@example.invalid",
    "GIT_TERMINAL_PROMPT": "0",
}


def isolate_git(cleanup):
    """Make git in this process (and its children) independent of the machine.

    Call it from a test's setUp with `self.addCleanup` as the argument.
    """
    saved = {key: os.environ.get(key) for key in ISOLATED_ENV}
    os.environ.update(ISOLATED_ENV)

    def restore():
        for key, value in saved.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    cleanup(restore)


def require_git(test):
    """Fail - do not skip - when git is not on this process's PATH.

    These tests exist to assert what `dev.py worktrees` and `dev.py shots` do
    to real repositories, so running none of them and reporting green is the
    one outcome worth failing over. The usual cause is Windows: `dev.py` runs
    tools inside MSYS2 MinGW64, whose PATH can hide the Git for Windows
    install, so `ctest -L tooling` from a `dev.py`-spawned shell sees no git
    even though the user's own shell does.
    """
    if not shutil.which("git"):
        test.fail("git is not on this process's PATH, and these tests drive real git."
                  " Run them from a shell that sees git (on Windows: PowerShell, not an MSYS2"
                  " one, and not `dev.py`'s own environment), or install MSYS2's `git` package.")


def run(*args, cwd):
    """Run git and return its stdout, or fail the test with git's message."""
    proc = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True, text=True)
    if proc.returncode != 0:
        raise AssertionError(f"git {' '.join(args)} failed: {proc.stderr or proc.stdout}")
    return proc.stdout


def git_fails(*args, cwd) -> bool:
    """Whether a git command fails - for asserting that nothing was pushed."""
    return subprocess.run(["git", *args], cwd=str(cwd), capture_output=True).returncode != 0


def quiet(fn, *args, **kwargs):
    """Run a command that reports progress, without the progress in the log."""
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return fn(*args, **kwargs)


def make_repo(tmp, origin_name="origin.git", directory="repo", branch="master"):
    """A repository with one commit, a local bare remote and a fetched master.

    Four commands, not eleven: identity comes from the environment (see
    ISOLATED_ENV) and the branch name is given to `init`, so nothing else has
    to be configured before a commit works.
    """
    origin = tmp / origin_name
    repo = tmp / directory
    run("init", "--quiet", "--bare", str(origin), cwd=tmp)
    run("init", "--quiet", "-b", branch, str(repo), cwd=tmp)
    run("remote", "add", "origin", str(origin), cwd=repo)
    (repo / "README.md").write_text("one\n", encoding="utf-8")
    (repo / ".gitignore").write_text("_bin/\n", encoding="utf-8")  # as the real repo has
    run("add", "README.md", ".gitignore", cwd=repo)
    run("commit", "--quiet", "-m", "first", cwd=repo)
    run("push", "--quiet", "-u", "origin", branch, cwd=repo)
    run("fetch", "--quiet", "origin", cwd=repo)
    return repo, origin
