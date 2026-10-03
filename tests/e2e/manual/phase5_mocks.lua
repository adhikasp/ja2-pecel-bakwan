-- Screenshots of the Phase 5 wireframes (M2; assets/ui/mocks/phase5/*.rml, written by tools/ui/phase5_mocks.py) over
-- the real tactical world, at several output sizes, with the layout audit logged. The world is its own layer
-- (worldzoom 2), so the mocks can hide the legacy HUD and show the whole world behind. Not a test; run it by hand on
-- the save that tests/e2e/manual/phase5_audit.lua writes:
--   python tools/ja2ctl.py run tests/e2e/manual/phase5_mocks.lua --saves <dir> --load phase5_squad --out <dir> [--arg combat,menus] [--arg 1920x1080]
local only = ja2.args and ja2.args[1] ~= "" and ja2.args[1] or nil
local mocks = { "squadbar", "combat", "inventory", "itemdesc", "money", "menus", "dialogue", "msglog", "overhead", "placement" }
local sizes = { "1920x1080", "3440x1440", "1280x720" }
if ja2.args and ja2.args[2] then sizes = { ja2.args[2] } end

ja2.waitScreen("GAME_SCREEN")
ja2.waitIdle()
for _, size in ipairs(sizes) do
	ja2.setVideo{ res = size, uiscale = 1, worldzoom = 2 }
	ja2.waitIdle()
	ja2.key("/") -- centre the world on the selected merc
	ja2.waitIdle()
	for _, m in ipairs(mocks) do
		if not only or only == "all" or only:find(m, 1, true) then
			if m == "overhead" then ja2.key("insert") ja2.waitIdle() end
			if m == "placement" then ja2.debug("placement") ja2.wait(800) end
			ja2.move(ja2.screenSize().w - 3, 3)
			ja2.debug("mock", "phase5/" .. m)
			ja2.wait(300)
			ja2.screenshot(m .. "_" .. size .. ".png")
			for _, p in ipairs(ja2.layoutProblems()) do ja2.log(m .. " " .. size .. ": " .. p) end
			ja2.log(m .. " " .. size .. " warnings: " .. tostring(ja2.nativeUi().warnings))
			ja2.key("ESC")
			ja2.wait(200)
			if m == "overhead" then ja2.key("insert") ja2.waitIdle() end
		end
	end
end
