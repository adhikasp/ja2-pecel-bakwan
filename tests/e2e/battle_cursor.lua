-- The native tactical cursor as data (issue #318, docs/plan/native-tactical.md "CursorModel"): what the pointer
-- means over a tile is asserted through ja2.cursor() { shown, mode, shape, tone, marker, ap, hit, why, path, ... },
-- with no screenshot. The mapping of every legacy cursor id is unit-tested in NativeUI/CursorModel_unittest.cc; this
-- drives it from the real Handle_UI on live soldiers in a staged fight.
--
-- Run: python tools/ja2ctl.py run tests/e2e/battle_cursor.lua --isolated --res 1920x1080
package.path = (debug.getinfo(1, "S").source:match("^@(.*[/\\])") or "") .. "?.lua;" .. package.path
local campaign = require("lib.campaign")
local battle = require("lib.battle")

if ja2.screen() ~= "GAME_SCREEN" then
	ja2.waitScreen("MAINMENU_SCREEN")
	campaign.startWithTeam({ "Ivan", "Barry", "Grizzly", "Buns" }, "One Week", true)
end
ja2.waitIdle()
ja2.setVideo{ res = "1920x1080", uiscale = 1, worldzoom = 2 }
ja2.waitIdle()
battle.stage{ enemies = { count = 3, distance = 7 } }
ja2.key("/") -- centre the world on the selected merc
ja2.wait(400)
ja2.waitIdle()

-- The cursor state after the pointer rests at (x, y) long enough for the path to plot.
local function hover(x, y)
	ja2.move(x, y)
	ja2.wait(1100)
	return ja2.cursor()
end

local function line(c, kind)
	for _, l in ipairs(c.lines) do
		if l.kind == kind then return l end
	end
	return nil
end

local WORLD_COLS = 160
local sel = battle.merc(1)
ja2.expect(sel, "a merc is selected")
local function near(dx, dy) return sel.gridNo + dx + dy * WORLD_COLS end
local sw, sh = ja2.screenSize().w, ja2.screenSize().h

-- --- over the HUD there is no world cursor ------------------------------------------------------------
local c = hover(560, sh - 80)
ja2.expect(not c.shown, "over the bottom bar the world has no cursor (shown=" .. tostring(c.shown) .. ")")
ja2.expect(c.path == nil and c.markerAt == nil, "and no path or marker")

-- --- a move in turn-based combat ----------------------------------------------------------------------
-- A tile a few steps away that the team can walk to: try some until one is a plain move.
local move
for _, d in ipairs({ { 3, 5 }, { -3, 5 }, { 4, 3 }, { -4, 3 }, { 2, 6 }, { 0, 6 }, { -5, 2 } }) do
	local ok, x, y = pcall(ja2.gridPos, near(d[1], d[2]))
	if ok then
		c = hover(x, y)
		if c.mode == "move" and c.path then move = c break end
	end
end
ja2.expect(move, "some tile near the team is a plain move with a path")
ja2.expect(move.shown and move.marker == "tile", "the move puts a marker on the tile (" .. tostring(move.marker) .. ")")
ja2.expect(move.shape == "walk" or move.shape == "run" or move.shape == "sneak" or move.shape == "crawl",
	"a move has a move shape (" .. tostring(move.shape) .. ")")
ja2.expect(move.ap > 0 and move.ap <= move.apLeft, "its cost is within the turn's points: " .. move.ap .. " of " .. move.apLeft)
ja2.expect(move.tone == "ok" and move.why == "", "it is a plain yes")
ja2.expect(move.path.steps >= 1 and move.path.solid == move.path.steps, "every step of the path is this turn's")
ja2.expect(not move.path.beyond and move.path.next == 0, "nothing spills into the next turn")
ja2.expect(move.chip and line(move, "ap"), "the chip carries the AP line")
ja2.expect(move.markerAt and move.markerAt.x > 0, "the marker has a world position")

-- --- a move that costs more than the turn has ---------------------------------------------------------
local far
for _, d in ipairs({ { -14, 12 }, { 14, 12 }, { -18, 6 }, { 0, 18 }, { 18, 0 }, { -12, 16 } }) do
	local ok, x, y = pcall(ja2.gridPos, near(d[1], d[2]))
	if ok then
		c = hover(x, y)
		if c.mode == "move" and c.path and c.path.beyond then far = c break end
	end
end
if far then
	ja2.expect(far.tone == "warn" and far.why == "next_turn", "a long move is a warning that it goes on next turn")
	ja2.expect(far.path.solid < far.path.steps, "only the first part of the path is this turn's")
	ja2.expect(far.path.now == far.apLeft, "this turn pays what the merc has: " .. far.path.now .. " = " .. far.apLeft)
	ja2.expect(far.path.now + far.path.next == far.path.total, "now and next add up to the cost")
	ja2.expect(line(far, "ap_split"), "the chip splits the cost into now and next turn")
else
	ja2.log("no tile on screen is beyond one turn's points; the spill case is covered by CursorModel_unittest")
end

-- --- an enemy under the pointer -----------------------------------------------------------------------
local enemy = battle.enemies()[1]
ja2.expect(enemy, "there is an enemy")
local ok, ex, ey = pcall(ja2.gridPos, enemy.gridNo)
if ok then
	-- his head and body are above the tile's centre
	for _, dy in ipairs({ -30, -20, -40, -10 }) do
		c = hover(ex, ey + dy)
		if c.mode == "target" then break end
	end
	ja2.expect(c.mode == "target", "the pointer over an enemy aims (" .. tostring(c.mode) .. ")")
	ja2.expect(c.shape == "fire" or c.shape == "burst", "an attack has an attack shape (" .. tostring(c.shape) .. ")")
	ja2.expect(c.tone == "foe" or c.tone == "no" or c.tone == "warn", "and a tone that says what it is: " .. tostring(c.tone))
	ja2.expect(c.target ~= "", "it names the target")
	ja2.expect(c.hit >= 0 and c.hit <= 100, "the chance to hit is shown (" .. tostring(c.hit) .. ")")
	ja2.expect(c.ap > 0, "an attack costs points in combat (" .. tostring(c.ap) .. ")")
	ja2.expect(line(c, "hit") and line(c, "ap"), "the chip carries hit and AP")
	ja2.expect(c.chip, "and is shown")
end

-- --- a held item over the world -----------------------------------------------------------------------
ja2.debug("hand", { merc = sel.name, item = "FIRSTAIDKIT", count = 1 })
ja2.waitIdle()
-- (the detail panel opens with it, over the left of the world: the pointer rests on the open ground at the right)
c = hover(sw - 420, 330)
ja2.expect(c.mode == "item", "with an item in the hand the world is a drop target (" .. tostring(c.mode) .. ")")
ja2.expect(c.shape == "drop" or c.shape == "give", "shape drop or give (" .. tostring(c.shape) .. ")")
ja2.expect(c.marker == "tile" or c.shape == "give", "a drop marks its tile (" .. tostring(c.marker) .. ")")
-- put it back so the fight can end cleanly
ja2.inventoryOp("close", {})
ja2.log("battle_cursor: ok")
