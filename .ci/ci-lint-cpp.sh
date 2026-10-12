#!/usr/bin/env bash
#
# Diff-scoped C++ checks for CI: clang-format on the changed lines and
# clang-tidy on the changed translation units.
#
# Whole-tree versions of both are deliberately NOT run:
#   - clang-format over the whole tree would rewrite ~275,000 lines (the tree
#     carries two hand styles and was never clang-formatted), which would bury
#     any real change;
#   - clang-tidy over the whole tree is ~3.1 s/TU across 587 TUs, i.e. ~30
#     minutes, and far too noisy to be a gate.
# Only what this pull request touches is checked. Known-unfixable legacy
# findings therefore never fail CI unless the line that carries them is itself
# edited, and a check is never weakened to make a run green.
#
# Called from .ci/ci-build.sh. Arguments: <repo-root> <build-dir>.
set -e

repo="${1:-.}"
build="${2:-ci-build}"
cd "$repo"

# --- what changed, relative to what -----------------------------------------
# A pull request uses its base branch; a push uses the commit it replaced.
# Anything else (a tag, the first push of a branch) has no meaningful base and
# the check is skipped rather than compared against something arbitrary.
base=""
if [[ -n "${CI_BASE_REF:-}" ]]; then
  base="${CI_BASE_REF}"
  git fetch --no-tags origin "${base}" >/dev/null 2>&1 || true
  base="origin/${base}"
fi
if [[ -z "${base}" || "$(git rev-parse --verify -q "${base}^{commit}" || true)" == "" ]] \
   && git rev-parse --verify -q origin/master^{commit} >/dev/null 2>&1; then
  base=origin/master
fi
if [[ -z "${base}" || "$(git rev-parse --verify -q "${base}^{commit}" || true)" == "" ]]; then
  echo "ci-lint-cpp: no base ref to diff against; skipping C++ checks"
  exit 0
fi
base="$(git merge-base "${base}" HEAD)"
echo "ci-lint-cpp: base ${base}, HEAD $(git rev-parse --short HEAD)"

# --- clang-format, changed lines only ---------------------------------------
# `git clang-format --diff` prints the diff clang-format would apply to the
# lines this change touched; no diff header is a pass.
if command -v clang-format >/dev/null 2>&1 && command -v git-clang-format >/dev/null 2>&1; then
  echo "## clang-format (changed lines only) ##"
  fmt_diff="$(git clang-format --diff "${base}" -- '*.cc' '*.h' 2>/dev/null || true)"
  # `git clang-format` prints "no modified files to format" on stdout when the
  # change touches no C++ file, so an actual diff is recognised by its header.
  if printf '%s' "${fmt_diff}" | grep -q '^diff --git '; then
    printf '%s\n' "${fmt_diff}"
    echo "ci-lint-cpp: clang-format would change the lines above."
    echo "  Stage the change, run 'git clang-format ${base}', and commit the result."
    exit 1
  fi
else
  echo "ci-lint-cpp: clang-format/git-clang-format not on PATH; skipping format check"
fi

# --- clang-tidy, changed translation units only ------------------------------
if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "ci-lint-cpp: clang-tidy not on PATH; skipping tidy check"
  exit 0
fi
if [[ ! -e "${build}/compile_commands.json" ]]; then
  echo "ci-lint-cpp: ${build}/compile_commands.json not found; skipping tidy check"
  exit 0
fi

python_bin=""
for c in python3 python; do
  if command -v "$c" >/dev/null 2>&1; then python_bin="$c"; break; fi
done
if [[ -z "${python_bin}" ]]; then
  echo "ci-lint-cpp: python not on PATH; skipping tidy check"
  exit 0
fi

echo "## clang-tidy (changed lines only) ##"
"${python_bin}" - "${base}" "${build}" <<'PY'
import json
import os
import subprocess
import sys

base, build = sys.argv[1], sys.argv[2]

# The C++ sources this change touched, and the new-side line ranges of each.
# `-U0` keeps the hunks tight so a filter entry is exactly the changed lines.
diff = subprocess.run(
    ["git", "diff", "-U0", "--no-color", base, "HEAD", "--", "*.cc", "*.h"],
    capture_output=True, text=True, check=True).stdout

filter_entries = []
current = None
for line in diff.splitlines():
    if line.startswith("+++ b/"):
        current = line[len("+++ b/"):]
        if current == "/dev/null":
            current = None
            continue
        # clang-tidy matches the filter name against the path it prints in a
        # diagnostic, which uses the host's separator. On Windows that is a
        # backslash; `git diff` and the POSIX hosts CI runs on use a forward
        # slash, so only the drive-letter form needs rewriting.
        name = os.path.abspath(current)
        if len(name) > 1 and name[1] == ":":
            name = name.replace("/", "\\")
        filter_entries.append({"name": name, "lines": []})
    elif line.startswith("@@") and filter_entries:
        # @@ -a,b +c,d @@  ->  new lines c .. c+d-1
        try:
            new = line.split("+", 1)[1].split(" ", 1)[0]
            start = int(new.split(",")[0])
            count = int(new.split(",")[1]) if "," in new else 1
        except (IndexError, ValueError):
            continue
        if count > 0:
            filter_entries[-1]["lines"].append([start, start + count - 1])

entries = [e for e in filter_entries if e["lines"]]
if not entries:
    print("ci-lint-cpp: no changed C++ lines; nothing to analyse")
    sys.exit(0)

files = [e["name"] for e in entries]
line_filter = json.dumps(entries)
cmd = ["clang-tidy", "-p", build, f"--line-filter={line_filter}", *files]
print("clang-tidy on:", " ".join(os.path.relpath(f) for f in files))
sys.exit(subprocess.run(cmd).returncode)
PY
