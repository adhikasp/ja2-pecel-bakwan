#!/usr/bin/env python3
"""Unit tests for how a failing tour reports itself (tools/ja2ctl.py, tests/e2e/check_resolution.py).

Issue #45: a resolution test that failed while the matrix ran in parallel printed

    automation: .../saveload_parity.lua passed (3395 frames, 62583 ms game time)
    saveload_parity @ 2560x1080: script failed with exit code 4294967295

- a script that had passed, called a script failure, with a code (0xffffffff, i.e. -1, what
PowerShell's Stop-Process and .NET's Process.Kill leave behind) that reads as a crash. Nothing in
that message said whether the script, the golden compare or the process had failed.

The exit-code vocabulary lives in ja2ctl - the program that runs the game and owns the driver's
codes - and check_resolution reads it from there, so neither can drift alone. These tests pin the
vocabulary, the sentence the harness builds from it, and the game's own verdict line that the
sentence is built on.

    python -m unittest discover -s tools/tests
"""

import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "tests" / "e2e"))

import ja2ctl  # noqa: E402

AUTOMATION_CC = REPO / "src" / "game" / "Automation" / "Automation.cc"

# check_resolution imports numpy and Pillow for the pixel compare, and exits (not raises) when the
# venv is missing - the resolution suite needs that venv too. Its half of the tests is skipped on a
# bare interpreter rather than failing the tooling suite over a dependency it shares with the
# suite under test.
SKIP = ""
try:
    import check_resolution  # noqa: E402
except SystemExit as exc:
    check_resolution = None
    SKIP = str(exc)


class DescribeExitCode(unittest.TestCase):
    """ja2ctl.describe_exit_code: the driver's own result, or what happened instead."""

    def test_the_drivers_codes_are_its_own_result(self):
        self.assertEqual(ja2ctl.EXIT_MEANINGS[0], "passed")
        for code, meaning in ja2ctl.EXIT_MEANINGS.items():
            with self.subTest(code=code):
                text = ja2ctl.describe_exit_code(code)
                self.assertIn("the driver's own result", text)
                self.assertIn(meaning, text)

    def test_an_external_kill_is_not_a_script_result(self):
        # What Windows hands back for a process killed by PowerShell Stop-Process / .NET
        # Process.Kill, and the value the misleading message in issue #45 carried.
        text = ja2ctl.describe_exit_code(0xFFFFFFFF)
        self.assertIn("terminated externally", text)
        self.assertIn("-1", text)

    def test_a_crash_is_not_a_script_result(self):
        for code in (0xC0000005, 0xC00000FD):  # access violation, stack overflow
            with self.subTest(code=code):
                text = ja2ctl.describe_exit_code(code)
                self.assertIn("unhandled exception", text)
                self.assertIn(f"0x{code:08x}", text)

    def test_a_signal_is_not_a_script_result(self):
        # POSIX: Python reports a process killed by signal N as -N. -1 is SIGHUP, not the Windows
        # kill value: the two spellings mean different things and only the platform picks one.
        self.assertIn("killed by signal 1", ja2ctl.describe_exit_code(-1))
        self.assertIn("killed by signal 9", ja2ctl.describe_exit_code(-9))

    def test_a_code_nobody_defines_is_named_as_such(self):
        text = ja2ctl.describe_exit_code(7)
        self.assertIn("0x00000007", text)
        self.assertIn("never produces", text)


@unittest.skipIf(check_resolution is None, f"check_resolution is unavailable: {SKIP}")
class FailureReason(unittest.TestCase):
    """check_resolution.failure_reason: which of the three layers a nonzero exit belongs to."""

    def test_the_reporter_case_names_the_process_not_the_script(self):
        reason = check_resolution.failure_reason(0xFFFFFFFF, "passed", 5)
        self.assertIn("the script passed", reason)
        self.assertIn("terminated externally", reason)
        self.assertIn("5 golden image(s) marked, none compared", reason)
        self.assertNotIn("the script failed", reason)

    def test_a_real_script_failure_says_so(self):
        reason = check_resolution.failure_reason(1, "FAILED", 0)
        self.assertIn("the script failed", reason)
        self.assertIn("a script expectation failed", reason)

    def test_a_death_with_no_verdict_says_so(self):
        reason = check_resolution.failure_reason(7, None, 0)
        self.assertIn("before the script reached its verdict", reason)
        self.assertNotIn("the script failed", reason)

    def test_the_compare_has_its_own_code_and_message(self):
        # Not one of the driver's codes: a reader (or a script) can tell the golden compare from
        # the script without parsing English.
        self.assertNotIn(check_resolution.EXIT_GOLDEN_MISMATCH, ja2ctl.EXIT_MEANINGS)


@unittest.skipIf(check_resolution is None, f"check_resolution is unavailable: {SKIP}")
class VerdictLine(unittest.TestCase):
    """The regex that reads the tour's verdict, against the line the game actually prints."""

    def test_it_reads_both_verdicts(self):
        m = check_resolution.VERDICT.match(
            "automation: C:/x/tests/e2e/saveload_parity.lua passed (3395 frames, 62583 ms game time)")
        self.assertEqual(m.group(1), "passed")
        self.assertEqual(check_resolution.VERDICT.match("automation: /x/t.lua FAILED (exit 2)").group(1), "FAILED")
        self.assertEqual(check_resolution.VERDICT.match("automation: /x/t.lua FAILED: 2 check(s) failed").group(1),
                         "FAILED")

    def test_it_ignores_everything_else(self):
        for line in (
            "automation: running /x/t.lua",
            "info: the script passed nothing",
            "[automation] /x/t.lua failed: some error",
        ):
            with self.subTest(line=line):
                self.assertIsNone(check_resolution.VERDICT.match(line))

    def test_the_game_still_prints_those_lines(self):
        # The drift guard: this file and Automation.cc have to agree on the wording of the verdict
        # line, and nothing else joins them. Change the format string and this fails.
        source = AUTOMATION_CC.read_text(encoding="utf-8")
        self.assertIn("automation: %s passed (", source)
        self.assertIn("automation: %s FAILED", source)


if __name__ == "__main__":
    unittest.main()
