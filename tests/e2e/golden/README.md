# Golden screenshots

`<resolution>/<script>/<shot>.png`, compared by `tests/e2e/check_resolution.py`
(a pixel differs if any channel is off by more than 8/255; a shot fails if more
than 0.05% of its pixels differ). Run through `ctest -L resolution`.

Regenerate after an intentional visual change (from the build dir):

    python ../tests/e2e/check_resolution.py ../tests/e2e/laptop_tour.lua 1280x720 --update

or set `JA2_UPDATE_GOLDEN=1` and run `ctest -L resolution`. Review the PNGs
before committing.

Full-screen tactical shots (`landed.png`, `moved.png`) are stored only at
640x480 and 1280x720: at wider sizes they are 4-10 MB each. At every resolution
the tours still run and call `ja2.assertInsideScreen()`.
