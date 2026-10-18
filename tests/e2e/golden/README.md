# Golden screenshots

`<resolution>/<script>/<shot>.png`, compared by `tests/e2e/check_resolution.py`
(a pixel differs if any channel is off by more than 8/255; a shot fails if more
than 0.05% of its pixels differ). Run through `ctest -L resolution`.

**These goldens are captured from a real game install and are not comparable to a
synthetic `Data/`.** A synthetic run (the generated tree `ctest -L e2e-synthetic`
boots, see [../../tools/assets/synthetic.py](../../tools/assets/synthetic.py)) draws
placeholder art, so its pixels are its own — the resolution suite stays on a real
install and nothing here changes for it (issue #302). What a synthetic run does
check is layout: `ja2.assertInsideScreen()` runs on every shot inside the e2e
scripts themselves, not in the comparator.

`ctest -L resolution` is the default sweep and runs only 1080p. Run the full
matrix (`ctest -R resolution_`) only when the change alters how the game
renders — a new menu, a texture/asset, or the renderer itself. See
[../README.md](../README.md#resolution-matrix).

A **localisation run** (`ctest -L localisation`, the DE/RU/PL laptop audit of
[#175](https://github.com/adhikasp/ja2-pecel-bakwan/issues/175)) never comes
through here: it drives the tours with `ja2ctl` and `-- -resversion`, registers
and compares no golden image, and is a layout audit only. There is no golden set
per language, and no language dimension in this directory.

Regenerate after an intentional visual change (from the build dir):

    python ../tests/e2e/check_resolution.py ../tests/e2e/laptop_tour.lua 1280x720 --update

or set `JA2_UPDATE_GOLDEN=1` and run `ctest -R resolution_` (all resolutions) or
`ctest -L resolution` (1080p only). Review the PNGs
before committing.

`--update` skips the (script, resolution) pairs that would register no golden
image anyway: a script whose shots are all `"small"` at a wide size, or one
that takes no golden shot at all. That is about a third of the matrix, and
running it would cost minutes of tour per test to write nothing. The
comparison run still runs those tests — they still check the layout. Pass
`--force` to run them under `--update` too.

Full-screen tactical shots (`landed.png`, `moved.png`) are stored only at
1280x720: at wider sizes they are 4-10 MB each. At every resolution
the tours still run and call `ja2.assertInsideScreen()`.
The smallest output the game runs at is 1280x720 (issue #324): there are no 640x480 goldens.

## What a failing test says

A resolution test runs a tour and then compares its shots, so a nonzero exit
can come from three places. Each one has its own message, decided by the tour's
own `passed`/`FAILED` line (the driver prints one for every result the script
produced) rather than by the exit code:

| Message | What failed |
|---|---|
| `the script failed (exit code N: …)` | the tour — a failed expectation, the game throwing |
| `the tour ended before a verdict (exit code N: …)` | the tour, before it could report: a bad path or save, a crash at startup, a wait or the watchdog timing out |
| `the golden compare failed, the script did not:` | the pixels |
| `the script passed and the process died after it …` / `the process died before the script reached its verdict …` | the process — a kill or a crash, never the script |

The last row is the one worth knowing. It used to print `script failed` for
anything nonzero: an externally killed process reports 4294967295
(`0xffffffff` = -1, what PowerShell's `Stop-Process` and .NET's `Process.Kill`
leave behind), which reads as a crash, and a killed process can equally report
the code the driver uses for a failed expectation — Windows' `taskkill /F`
uses 1 — which is why the verdict decides and the code only says what the
process did (issue #45).

## Why a golden image is portable

A golden image is only comparable if the same tour produces the same pixels on
every machine. Two things the UI shows are not a function of the game's state, so
`check_resolution.py` pins both on every run (including `--update`):

| Pinned | Value | Why |
|---|---|---|
| `-freeze-wall-clock` | `2026-01-15T12:00:00Z` | A save's filename (`SaveLoadNewFileName`) and its "Today HH:MM" label are wall-clock text. A frozen instant is formatted in **UTC**, because the same instant read in the build machine's timezone is a different string in Berlin than in Singapore. |
| `-version-label` | `Pecel Bakwan golden` | `CMakeLists.txt` appends `git rev-parse --short HEAD` to the version. The sha is right for a bug report and useless here: the main menu, the message box and every other screen showing it would differ on every commit. |

Both default to the real value when the flags are absent, so a normal run is
unaffected. If you add a screen that shows the version, the travel is that the
golden then depends on `GOLDEN_VERSION` in `check_resolution.py` — change that
constant and regenerate the affected goldens.

Text rasterization itself is portable and needs no pinning: the same glyph comes
out byte-identical across FreeType 2.11–2.14, with subpixel hinting on or off,
on x86-64 and aarch64.

## Comparison dependencies

`check_resolution.py` decodes PNGs with Pillow and compares pixels with numpy.
Both are locked in the repo's `uv` environment (`pyproject.toml` / `uv.lock`):
run `uv sync` once in the repo root and reconfigure cmake so `ctest` picks up
`.venv` (see [COMPILATION.md](../../COMPILATION.md#python-tooling)). A shot
whose bytes are already identical to its golden skips decoding entirely.
