-- Screenshots of the native-first tactical wireframes (docs/plan/native-tactical.md, issue #316;
-- assets/ui/mocks/tactical/*.rml, written by tools/ui/native_tactical_mocks.py) over the live tactical HUD and world,
-- with the layout audit logged. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/native_tactical_mocks.lua --isolated --out <dir> [--arg drag,talk] [--arg 1920x1080]
-- The mocks place HUD-relative parts in dp measured at 1920x1080, so they hold on 16:9 outputs.
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")
local battle = require("lib.battle")

local only = ja2.args and ja2.args[1] ~= "" and ja2.args[1] or nil
local sizes = { "1920x1080", "1280x720" }
if ja2.args and ja2.args[2] then sizes = { ja2.args[2] } end
local function want(m) return not only or only == "all" or only:find(m, 1, true) end

if ja2.screen() ~= "GAME_SCREEN" then
	ja2.waitScreen("MAINMENU_SCREEN")
	campaign.startWithTeam({ "Ivan", "Barry", "Grizzly", "Buns" }, "One Week", true)
end
ja2.waitIdle()

local function detail(open)
	local v = ja2.viewModel("tactical")
	if v.detail ~= open then ja2.click{ id = "tac.inventory" } ja2.waitIdle() end
end

local function shoot(m, size)
	ja2.move(ja2.screenSize().w - 3, 3)
	ja2.debug("mock", "tactical/" .. m)
	ja2.wait(300)
	ja2.screenshot(m .. "_" .. size .. ".png")
	for _, p in ipairs(ja2.layoutProblems()) do ja2.log(m .. " " .. size .. ": " .. p) end
	ja2.key("ESC")
	ja2.wait(200)
end

local function setup(size)
	ja2.setVideo{ res = size, uiscale = 1, worldzoom = 2 }
	ja2.waitIdle()
	ja2.key("/") -- centre the world on the selected merc
	ja2.wait(400)
	ja2.waitIdle()
end

for _, size in ipairs(sizes) do
	setup(size)
	-- the live HUD as it is, for comparison (at 1280x720: the minimum output the plan proposes)
	if want("live") then
		detail(true)
		ja2.screenshot("live_" .. size .. ".png")
	end
	detail(false)
	for _, m in ipairs{ "cursors", "talk", "exit", "overlays" } do
		if want(m) then shoot(m, size) end
	end
	detail(true)
	for _, m in ipairs{ "drag_slot", "drag_give", "drag_world", "stack", "keyring" } do
		if want(m) then shoot(m, size) end
	end
	detail(false)
	if want("overhead") then
		ja2.key("insert") ja2.waitIdle()
		shoot("overhead", size)
		ja2.key("insert") ja2.waitIdle()
	end
end

-- combat and placement change the game: last, at the first size only
local size = sizes[1]
setup(size)
if want("target") or want("move") then
	battle.stage{
		enemies = { count = 3, class = "administrator", weapon = "GLOCK_17", distance = 7 },
		our = {
			{ name = "Ivan", grid = 4871, direction = 0 },
			{ name = "Barry", grid = 4711, direction = 0 },
			{ name = "Grizzly", grid = 5030, direction = 0 },
		},
	}
	ja2.waitIdle()
	ja2.key("/")
	ja2.wait(400)
	if want("target") then
		-- the target mock is anchored to the first enemy: put it in the middle of the view
		ja2.debug("camera", battle.enemies()[1].gridNo)
		ja2.wait(400)
		shoot("target", size)
		ja2.key("/")
		ja2.wait(400)
	end
	if want("move") then shoot("move", size) end
end
if want("placement") then
	ja2.debug("placement")
	ja2.wait(800)
	shoot("placement", size)
end
