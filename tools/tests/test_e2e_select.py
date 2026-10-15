#!/usr/bin/env python3
"""Unit tests for the e2e selector and report (tools/e2e_select.py).

Most of what matters here is pure: which areas a path maps to, what an unmapped
path does, how CTest's own JSON and ctest's JUnit report are read. The path half
is then driven against a throwaway repository, because `--changed` is a git
question and only real git answers it.

The last class is the **drift guard** (issue #169): the CTest area labels are
declared in CMakeLists.txt and the movement vocabulary lives in
tools/e2e_select.py, which is one source of truth too many unless something
fails when they separate. The guard always cross-checks the two declarations,
and - when a configured build directory is there to ask - cross-checks them
against the labels CTest actually reports, which is what `dev.py e2e --changed`
joins on. It runs in CI: `.ci/ci-build.sh` runs `ctest -L '^tooling$'` after
configuring and building, so the build directory is present there.

    python -m unittest discover -s tools/tests
"""

import json
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import e2e_select  # noqa: E402
from gittest import isolate_git, make_repo, require_git, run  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
CMAKELISTS = REPO / "CMakeLists.txt"

# A test set good enough to exercise the join: the four smoke tests plus one
# test per area, all with the labels `parse_ctest_json` would strip `e2e`/`smoke`
# from. Deliberately not the real set - the real one is CTest's to report.
AREAS_FIXTURE = {
    "e2e_main_menu": set(),
    "e2e_mainmenu_parity": set(),
    "e2e_new_game_to_tactical": {"campaign", "laptop", "tactical"},
    "e2e_native_ui": set(),
    "e2e_battle_smoke": {"tactical"},
    "e2e_map_screen_tour": {"mapscreen"},
    "e2e_laptop_tour": {"laptop"},
    "e2e_campaign_state": {"campaign"},
    "e2e_loadout_spec": {"equipment"},
    "e2e_world_renderer": {"world-renderer"},
    "e2e_determinism": {"automation"},
    "e2e_tactical_layers_pipeline": {"tactical", "world-renderer"},
}


class PathMapping(unittest.TestCase):
    """areas_for_path / select: a path becomes areas, or nothing, or all."""

    def areas(self, path):
        return e2e_select.areas_for_path(path, AREAS_FIXTURE)

    def test_each_area_directory_maps_to_its_area(self):
        self.assertEqual({"tactical"}, self.areas("src/game/Tactical/x.cc"))
        self.assertEqual({"tactical"}, self.areas("src/game/TacticalAI/ai.cc"))
        self.assertEqual({"mapscreen"}, self.areas("src/game/Strategic/world.cc"))
        self.assertEqual({"laptop"}, self.areas("src/game/Laptop/laptop.cc"))
        self.assertEqual({"equipment"}, self.areas("src/game/Equipment/rules.cc"))
        self.assertEqual({"automation"}, self.areas("src/game/Automation/lua.cc"))

    def test_native_ui_names_the_screen_behind_the_prefix(self):
        # One directory, a screen per file: a wholesale nativeui mapping would
        # miss the tactical / mapscreen / laptop tests that exercise them.
        self.assertEqual({"tactical", "nativeui"},
                         self.areas("src/game/NativeUI/TacticalHud.cc"))
        self.assertEqual({"mapscreen", "nativeui"},
                         self.areas("src/game/NativeUI/MapScreenNative.cc"))
        self.assertEqual({"laptop", "nativeui"},
                         self.areas("src/game/NativeUI/LaptopScreen.cc"))
        self.assertEqual({"nativeui"}, self.areas("src/game/NativeUI/FrontEnd.cc"))
        self.assertEqual({"nativeui"}, self.areas("src/nativeui/UiCore.cc"))

    def test_renderer_and_tileengine_are_world_renderer(self):
        self.assertEqual({"world-renderer"}, self.areas("src/sgp/VideoGpu.cc"))
        self.assertEqual({"world-renderer"}, self.areas("src/sgp/shaders/world_raster.comp"))
        self.assertEqual({"world-renderer", "tactical"},
                         self.areas("src/game/TileEngine/tile.cc"))

    def test_a_test_script_maps_to_its_own_tests_areas(self):
        self.assertEqual({"campaign"}, self.areas("tests/e2e/campaign_state.lua"))
        # the check_* runners strip their prefix to find the e2e_<name> test
        self.assertEqual({"automation"}, self.areas("tests/e2e/check_determinism.py"))
        # a script compiled into several tests (tactical_layers.lua -> the
        # e2e_tactical_layers_* tests) unions their areas
        self.assertEqual({"tactical", "world-renderer"},
                         self.areas("tests/e2e/tactical_layers.lua"))

    def test_shared_fixtures_map_to_every_area(self):
        self.assertEqual(set(e2e_select.AREAS), self.areas("tests/e2e/lib/campaign.lua"))

    def test_an_unmapped_path_is_none_not_empty(self):
        # None is the safe direction: run everything.
        for path in ("src/externalized/Loader.cc", "assets/externalized/items.json",
                     "rust/src/lib.rs", "src/game/SaveLoadGame.cc", "src/sgp/Font.cc"):
            self.assertIsNone(self.areas(path), path)
        self.assertIsNone(self.areas("tests/e2e/brand_new.lua"))
        self.assertIsNone(self.areas("tests/e2e/check_resolution.py"))

    def test_select_keeps_smoke_and_the_mapped_areas(self):
        selection = e2e_select.select(["src/game/Tactical/x.cc"], AREAS_FIXTURE)
        self.assertFalse(selection.fallback)
        self.assertEqual({"tactical"}, selection.areas)
        for smoke in e2e_select.SMOKE:
            self.assertIn(f"e2e_{smoke}", selection.tests)
        self.assertIn("e2e_battle_smoke", selection.tests)
        self.assertNotIn("e2e_laptop_tour", selection.tests)

    def test_select_falls_back_to_all_on_an_unmapped_path(self):
        selection = e2e_select.select(
            ["src/game/Tactical/x.cc", "src/externalized/Loader.cc"], AREAS_FIXTURE)
        self.assertTrue(selection.fallback)
        self.assertIsNone(selection.areas)
        self.assertEqual(set(AREAS_FIXTURE), set(selection.tests))

    def test_ignored_paths_select_nothing(self):
        selection = e2e_select.select(["docs/plan/foo.md", "README.md"], AREAS_FIXTURE)
        self.assertFalse(selection.fallback)
        self.assertTrue(selection.ignored)
        self.assertEqual(set(), selection.areas)
        # smoke still runs, so a docs-only change is a fast sanity pass, not the suite
        self.assertEqual({f"e2e_{s}" for s in e2e_select.SMOKE}, set(selection.tests))

    def test_test_regex_matches_exactly(self):
        regex = e2e_select.test_regex(["e2e_a", "e2e_b_2"])
        self.assertTrue(re.match(regex, "e2e_a"))
        self.assertTrue(re.match(regex, "e2e_b_2"))
        self.assertIsNone(re.match(regex, "e2e_a_extra"))


class CTestJson(unittest.TestCase):
    """The label join reads CTest's own dump, not a copy of it."""

    DUMP = json.dumps({"kind": "ctestInfo", "tests": [
        {"name": "e2e_battle_smoke",
         "properties": [{"name": "LABELS", "value": ["e2e", "tactical"]},
                        {"name": "ENVIRONMENT", "value": ["JA2_BIN=/x"]}]},
        {"name": "e2e_new_game_to_tactical",
         "properties": [{"name": "LABELS", "value": ["e2e", "campaign", "laptop", "tactical", "smoke"]}]},
        {"name": "unittests", "properties": [{"name": "LABELS", "value": []}]},
        {"name": "spike_unittests", "properties": [{"name": "LABELS", "value": ["spike"]}]},
    ]})

    def test_only_e2e_tests_are_kept(self):
        areas = e2e_select.parse_ctest_json(self.DUMP)
        self.assertEqual({"e2e_battle_smoke", "e2e_new_game_to_tactical"}, set(areas))

    def test_tier_labels_are_stripped_from_the_areas(self):
        areas = e2e_select.parse_ctest_json(self.DUMP)
        self.assertEqual({"tactical"}, areas["e2e_battle_smoke"])
        self.assertEqual({"campaign", "laptop", "tactical"},
                         areas["e2e_new_game_to_tactical"])

    def test_broken_json_is_refused_with_the_reason(self):
        with self.assertRaises(e2e_select.CTestError):
            e2e_select.parse_ctest_json("not json")

    def test_the_runner_can_be_replaced(self):
        calls = []

        def fake_run(argv, cwd):
            calls.append((argv, cwd))
            return self.DUMP

        areas = e2e_select.ctest_test_areas("B", run=fake_run)
        self.assertIn("e2e_battle_smoke", areas)
        self.assertEqual([(["ctest", "--show-only=json-v1"], "B")], calls)


# A report shaped like ctest's `--output-junit`: the label rides in a
# `cmake_labels` property, and the time is an attribute.
JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="(empty)" tests="3" time="42">
\t<testcase name="e2e_battle_smoke" classname="e2e_battle_smoke" time="18.5" status="run">
\t\t<properties><property name="cmake_labels" value="e2e;tactical"/></properties>
\t</testcase>
\t<testcase name="e2e_main_menu" classname="e2e_main_menu" time="3.7" status="run">
\t\t<properties><property name="cmake_labels" value="e2e;nativeui;smoke"/></properties>
\t</testcase>
\t<testcase name="e2e_tactical_hud" classname="e2e_tactical_hud" time="9.25" status="run">
\t\t<properties><property name="cmake_labels" value="e2e;tactical"/></properties>
\t</testcase>
</testsuite>
"""


class JUnitReport(unittest.TestCase):
    def test_every_case_becomes_name_seconds_labels(self):
        entries = e2e_select.parse_junit(JUNIT)
        self.assertEqual(["e2e_battle_smoke", "e2e_main_menu", "e2e_tactical_hud"],
                         [e.name for e in entries])
        self.assertEqual(18.5, entries[0].seconds)
        self.assertEqual("e2e;nativeui;smoke", entries[1].labels)

    def test_the_report_is_slowest_first_with_a_total(self):
        lines = e2e_select.report_lines(e2e_select.parse_junit(JUNIT), count=2)
        self.assertIn("slowest 2 of 3", lines[0])
        self.assertIn("total test time 31.4s", lines[0])
        self.assertIn("e2e_battle_smoke", lines[1])
        self.assertIn("e2e_tactical_hud", lines[2])
        self.assertNotIn("e2e_main_menu", "".join(lines))

    def test_an_empty_report_says_nothing(self):
        self.assertEqual([], e2e_select.report_lines([]))

    def test_a_file_that_is_not_junit_is_refused(self):
        with self.assertRaises(e2e_select.SelectError):
            e2e_select.parse_junit("<not-a-testsuite")


class MissingTools(unittest.TestCase):
    """A tool that cannot be started is a clean error, not a traceback."""

    def test_a_missing_git_is_a_clean_error(self):
        with mock.patch.object(e2e_select.subprocess, "run", side_effect=FileNotFoundError("git")):
            with self.assertRaises(e2e_select.SelectError):
                e2e_select.changed_paths(".", "master")


class AgainstRealGit(unittest.TestCase):
    """`--changed` is a git question; only real git answers it."""

    def setUp(self):
        require_git(self)
        isolate_git(self.addCleanup)
        self.tmp = Path(tempfile.mkdtemp(prefix="ja2-select-test-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.repo, self.origin = make_repo(self.tmp)
        self.repo = Path(self.repo)

    def commit(self, *paths):
        run("add", *paths, cwd=self.repo)
        run("commit", "-q", "-m", "change", cwd=self.repo)

    def test_a_committed_change_on_a_branch_is_seen(self):
        run("checkout", "-q", "-b", "work", cwd=self.repo)
        target = self.repo / "src" / "game" / "Tactical" / "x.cc"
        target.parent.mkdir(parents=True)
        target.write_text("// x\n", encoding="utf-8")
        self.commit("src/game/Tactical/x.cc")
        self.assertIn("src/game/Tactical/x.cc",
                      e2e_select.changed_paths(self.repo, "master"))

    def test_uncommitted_and_untracked_files_are_seen(self):
        # An agent iterating has neither committed yet; both must be in the diff.
        (self.repo / "README.md").write_text("edited\n", encoding="utf-8")
        (self.repo / "src" / "game" / "Laptop").mkdir(parents=True)
        (self.repo / "src" / "game" / "Laptop" / "new.cc").write_text("// n\n", encoding="utf-8")
        paths = e2e_select.changed_paths(self.repo, "master")
        self.assertIn("README.md", paths)
        self.assertIn("src/game/Laptop/new.cc", paths)

    def test_a_missing_base_ref_is_an_error_not_an_empty_run(self):
        with self.assertRaises(e2e_select.SelectError):
            e2e_select.changed_paths(self.repo, "does-not-exist")

    def test_a_tactical_edit_selects_tactical_and_smoke(self):
        run("checkout", "-q", "-b", "work", cwd=self.repo)
        (self.repo / "tactical.cc").write_text("// t\n", encoding="utf-8")
        test_areas = dict(AREAS_FIXTURE)
        test_areas["e2e_battle_smoke"] = {"tactical"}
        paths = ["src/game/Tactical/foo.cc"]
        selection = e2e_select.select(paths, test_areas)
        self.assertEqual({"tactical"}, selection.areas)
        self.assertIn("e2e_battle_smoke", selection.tests)


# --- the drift guard --------------------------------------------------------

def cmake_area_entries():
    """{test name (no e2e_ prefix): set(areas)} from CMakeLists.txt's block."""
    text = CMAKELISTS.read_text(encoding="utf-8")
    m = re.search(r"set\(E2E_AREA_LABELS\b(.*?)\n\s*\)", text, re.S)
    assert m, "CMakeLists.txt has no E2E_AREA_LABELS block"
    entries = {}
    for raw in m.group(1).splitlines():
        line = raw.strip().strip('"')
        if not line or line.startswith("#"):
            continue
        name, _, areas = line.partition("=")
        entries[name] = set(filter(None, areas.split(",")))
    return entries


def cmake_smoke():
    text = CMAKELISTS.read_text(encoding="utf-8")
    m = re.search(r"foreach\(smoke\s+([^)]*)\)", text)
    assert m, "CMakeLists.txt has no smoke foreach"
    return set(m.group(1).split())


class DriftGuard(unittest.TestCase):
    """The two halves - CMake's labels and the selector's vocabulary - agree.

    This is what keeps a path-to-area table from silently rotting: adding an
    area to one side and not the other fails here, on the machine and in CI.
    """

    def test_the_declared_areas_are_exactly_the_selector_vocabulary(self):
        declared = set().union(*cmake_area_entries().values()) if cmake_area_entries() else set()
        self.assertEqual(set(e2e_select.AREAS), declared,
                         "CMakeLists.txt E2E_AREA_LABELS and e2e_select.AREAS disagree")

    def test_no_entry_names_an_unknown_area(self):
        unknown = set().union(*cmake_area_entries().values()) - set(e2e_select.AREAS)
        self.assertEqual(set(), unknown, f"CMakeLists.txt labels {unknown} are not in e2e_select.AREAS")

    def test_every_area_the_table_can_emit_is_a_real_label(self):
        # The static path rules may only speak the same vocabulary.
        used = set()
        for _pattern, areas in e2e_select.PATH_RULES:
            if isinstance(areas, tuple):
                used |= set(areas)
        self.assertLessEqual(used, set(e2e_select.AREAS))

    def test_the_smoke_tier_is_the_same_list_on_both_sides(self):
        self.assertEqual(set(e2e_select.SMOKE), cmake_smoke())
        entries = cmake_area_entries()
        for name in e2e_select.SMOKE:
            self.assertIn(name, entries, f"smoke test {name} has no area label")

    def test_the_real_ctest_labels_agree(self):
        """The authoritative check: what CTest reports against both declarations.

        Needs a configured build directory (CI has one, and so does any dev who
        ran `dev.py setup`); without one the declarations are still checked
        above, so the guard is never a no-op on the declaration side.
        """
        build_dir = self._build_dir()
        if build_dir is None:
            self.skipTest("no configured build dir to read real labels from")
        if not shutil.which("ctest"):
            self.skipTest("ctest is not on this process's PATH")
        labels = e2e_select.parse_ctest_labels(
            e2e_select.default_ctest_run(["ctest", "--show-only=json-v1"], build_dir))
        e2e = {name: ls for name, ls in labels.items() if "e2e" in ls}
        self.assertTrue(e2e, "ctest reported no e2e tests at all")

        real_areas = {name: ls - {"e2e", "smoke"} for name, ls in e2e.items()}
        for name, areas in real_areas.items():
            self.assertTrue(areas, f"{name} carries no area label")
            self.assertLessEqual(areas, set(e2e_select.AREAS),
                                 f"{name} carries an unknown label")
        self.assertLessEqual(set(e2e_select.AREAS), set().union(*real_areas.values()),
                             "an area in e2e_select.AREAS is carried by no test")

        for name, declared in cmake_area_entries().items():
            self.assertIn(f"e2e_{name}", real_areas,
                          f"CMakeLists.txt labels e2e_{name}, which ctest does not know")
            self.assertEqual(declared, real_areas[f"e2e_{name}"],
                             f"e2e_{name}: E2E_AREA_LABELS and ctest's labels disagree")

        real_smoke = {n for n, ls in e2e.items() if "smoke" in ls}
        self.assertEqual({f"e2e_{s}" for s in e2e_select.SMOKE}, real_smoke,
                         "the smoke label and e2e_select.SMOKE disagree")

    @staticmethod
    def _build_dir():
        for candidate in (Path.cwd(), REPO / "_bin", REPO / "build"):
            if (candidate / "CTestTestfile.cmake").exists():
                return candidate
        return None


if __name__ == "__main__":
    unittest.main()
