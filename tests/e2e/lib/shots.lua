-- Screenshots for the resolution matrix (ctest -R resolution_).
--
-- shots.take(name)         layout-check the screen (every mouse region and
--                          button must lie inside it), then screenshot.
-- shots.take(name, true)   same, and also register the shot as a golden
--                          image: tests/e2e/check_resolution.py compares it
--                          with tests/e2e/golden/<res>/<script>/<name>.
--
-- shots.take(name, "small") registers it only at widths <= 1280: full-screen
--                          tactical PNGs are 4-10 MB each at wider sizes, too
--                          heavy to keep in git. They are still layout-checked.
--
-- Shots are only registered when the run has `-arg golden=1`
-- (check_resolution.py passes it); plain `ja2ctl run` just takes the screenshot.
local shots = {}

function shots.take(name, golden)
	ja2.assertInsideScreen()
	local path = ja2.screenshot(name)
	if golden == "small" and ja2.screenSize().w > 1280 then golden = false end
	if golden and ja2.args and ja2.args[1] == "golden=1" then
		local dir = path:match("^(.*)[/\\][^/\\]+$") or "."
		local f = assert(io.open(dir .. "/golden.txt", "a"))
		f:write(name, "\n")
		f:close()
	end
	return path
end

return shots
