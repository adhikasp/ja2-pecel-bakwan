-- PR screenshots of the native tactical HUD (Phase 5) at every reference size and at UI scale 150% and 200%. Not a
-- test; run it by hand on the save tests/e2e/manual/phase5_audit.lua writes:
--   python tools/ja2ctl.py run tests/e2e/manual/phase5_shots.lua --isolated --saves <dir> --load phase5_squad --out <dir>
local sizes = { { "1280x720", 1 }, { "1920x1080", 1 }, { "2560x1440", 1 }, { "3440x1440", 1 }, { "3840x2160", 1 },
	{ "1920x1080", 1.5 }, { "1920x1080", 2 } }
local function vm() return ja2.viewModel("tactical") end

ja2.waitScreen("GAME_SCREEN")
ja2.waitIdle()
for _, sz in ipairs(sizes) do
	local res, scale = sz[1], sz[2]
	ja2.setVideo{ res = res }
	ja2.setUiScale(scale)
	ja2.waitIdle()
	ja2.key("/")
	ja2.waitIdle()
	local tag = res .. (scale ~= 1 and ("_ui" .. math.floor(scale * 100)) or "")
	local function shot(name)
		ja2.move(ja2.screenSize().w // 2, ja2.screenSize().h // 3)
		ja2.wait(200)
		ja2.screenshot(name .. "_" .. tag .. ".png")
		for _, p in ipairs(ja2.layoutProblems()) do ja2.log(name .. " " .. tag .. ": " .. p) end
	end
	shot("hud")
	ja2.key("`") ja2.waitIdle()
	shot("detail")
	ja2.click({ id = "tac.inv.slot[5]" }, { button = "right" }) ja2.waitIdle()
	if vm().desc then shot("itemdesc") ja2.click{ id = "tac.desc.done" } ja2.waitIdle() end
	ja2.click{ id = "tac.inv.money" } ja2.waitIdle()
	if vm().desc then
		ja2.click{ id = "tac.money.add100" } ja2.waitIdle()
		shot("money")
		ja2.click({ id = "tac.money.add100" }, { button = "right" }) ja2.waitIdle()
		ja2.click{ id = "tac.desc.done" } ja2.waitIdle()
	end
	ja2.key("`") ja2.waitIdle()
	ja2.key("h") ja2.waitIdle()
	shot("log")
	ja2.key("h") ja2.waitIdle()
end
ja2.setUiScale(1)
