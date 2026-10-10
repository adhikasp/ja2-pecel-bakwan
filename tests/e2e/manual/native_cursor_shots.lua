-- Screenshots of the native tactical cursor (issue #318) from the real game: a move with its path, a move that goes
-- on next turn, an attack with its chip, a blocked tile, a held item over the world, the pointer over the HUD and
-- the real-time move. Not a test; run it by hand:
--   python tools/ja2ctl.py run tests/e2e/manual/native_cursor_shots.lua --isolated --out <dir> --res 1920x1080 [--arg 1280x720]
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "../?.lua;" .. package.path
local campaign = require("lib.campaign")
local battle = require("lib.battle")

local size = ja2.args and ja2.args[1] ~= "" and ja2.args[1] or "1920x1080"
if ja2.screen() ~= "GAME_SCREEN" then
	ja2.waitScreen("MAINMENU_SCREEN")
	campaign.startWithTeam({ "Ivan", "Barry", "Grizzly", "Buns" }, "One Week", true)
end
ja2.waitIdle()
ja2.setVideo{ res = size, uiscale = 1, worldzoom = 2 }
ja2.waitIdle()
ja2.key("/")
ja2.wait(400)
ja2.waitIdle()
local sw, sh = ja2.screenSize().w, ja2.screenSize().h

local function hover(x, y)
	ja2.move(math.floor(x), math.floor(y))
	ja2.wait(1200)
	return ja2.cursor()
end

local function shot(name, c)
	ja2.screenshot(name .. "_" .. size .. ".png")
	ja2.log(("%s %s: mode=%s shape=%s tone=%s why=%s ap=%d/%d hit=%d chip=%s"):format(
		name, size, c.mode, c.shape, c.tone, c.why, c.ap, c.apLeft, c.hit, tostring(c.chip)))
end

-- real time: a plain walk, the marker and no chip
local c = hover(sw * 0.42, sh * 0.58)
shot("cursor_realtime_walk", c)

-- over the HUD the world has no cursor: the plain pointer
c = hover(560, sh - 80)
shot("cursor_over_hud", c)

battle.stage{ enemies = { count = 3, distance = 7 } }
ja2.key("/")
ja2.wait(400)
ja2.waitIdle()

-- a move this turn pays for
c = hover(sw * 0.42, sh * 0.58)
shot("cursor_move", c)

-- a move that goes on next turn
c = hover(sw * 0.16, sh * 0.74)
shot("cursor_move_next_turn", c)

-- a blocked tile
c = hover(sw * 0.57, sh * 0.42)
shot("cursor_blocked", c)

-- an enemy under the pointer
local enemy = battle.enemies()[1]
local ok, ex, ey = pcall(ja2.gridPos, enemy.gridNo)
if ok then
	for _, dy in ipairs({ -30, -20, -40, -10 }) do
		c = hover(ex, ey + dy)
		if c.mode == "target" then break end
	end
	shot("cursor_target", c)
end

-- a first aid kit in the hand over the open ground
ja2.debug("hand", { merc = battle.merc(1).name, item = "FIRSTAIDKIT", count = 1 })
ja2.waitIdle()
c = hover(sw - 420, 330)
shot("cursor_drop", c)
ja2.inventoryOp("close", {})
