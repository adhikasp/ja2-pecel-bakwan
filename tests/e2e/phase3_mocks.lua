-- Screenshots of the Phase 3 wireframes (M2; assets/ui/mocks/phase3/*.rml) through the native runtime at several
-- output sizes, with the layout audit logged. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/phase3_mocks.lua --isolated --out <dir> [--arg mainmenu,options]
local only = ja2.args and ja2.args[1]
local mocks = { "mainmenu", "options", "options_video", "options_audio", "options_controls", "options_access", "saveload", "saveload_save", "newgame", "loading" }
local sizes = { "1920x1080", "1280x720" }

ja2.waitScreen("MAINMENU_SCREEN")
ja2.waitIdle()
for _, size in ipairs(sizes) do
	ja2.setVideo{ res = size }
	ja2.waitIdle()
	for _, m in ipairs(mocks) do
		if not only or only:find(m, 1, true) then
			ja2.debug("mock", "phase3/" .. m)
			ja2.wait(300)
			ja2.move(ja2.screenSize().w - 3, 3)
			ja2.wait(200)
			ja2.screenshot(m .. "_" .. size .. ".png")
			for _, p in ipairs(ja2.layoutProblems()) do ja2.log(m .. " " .. size .. ": " .. p) end
			ja2.log(m .. " " .. size .. " warnings: " .. tostring(ja2.nativeUi().warnings))
			ja2.key("ESC")
			ja2.wait(200)
		end
	end
end
